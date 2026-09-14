# Heltec Mesh Node T1

Custom ZephCore board target for the Heltec Mesh Node T1.

Build:

```bash
west build -b heltec_t1 zephcore --pristine
```

Repeater/observer roles build the same way with the usual `EXTRA_CONF_FILE`
overlays; the T1 is a handheld tracker, so companion is the expected role.

## Hardware

- MCU: nRF52840 (1 MB flash, 256 kB RAM), Adafruit UF2 bootloader, SoftDevice
  s140 v6.1.1 layout (app @ `0x26000`, bootloader @ `0xF4000`)
- Radio: SX1262, 32 MHz TCXO on DIO3 @ 1.8 V, no PA/FEM
- RF path: separate RFO (TX) and RFI_P/RFI_N (RX) matched networks into a
  uPG2179 SPDT switch driven by SX1262 DIO2. Datasheet table 3.3: 21 ±1 dBm at
  the antenna in 863–870 and 902–928 MHz.
- GNSS: UC6580 (L1+L5, GPS/BDS/GLONASS/Galileo/QZSS/NavIC/SBAS), ATR2652 LNA
  and HDDL1L5RSS-B8 diplexer, 26 MHz TCXO, 32.768 kHz RTC crystal
- Display: ST7735S 0.96" TFT, 80×160 native / 160×80 landscape
- Buzzer: piezo element driven differentially by a PAM8904 charge-pump driver
- IMU: ICM-42607-P (accel + gyro) and MMC5983MA (magnetometer) — present but
  unused, see "Not enabled"
- Battery: 1850 mAh Li-ion, CN3165 charger (red LED is hardware-only)
- No external SPI/QSPI flash — storage is internal `/lfs` only
- IP65, 85 × 55 × 9.5 mm, 53 g

## Confirmed pinout

Source: official Heltec `Mesh_Node_T1_V1.0` schematic — the pin table on the
sheet, cross-checked against the U10/U11/U14/U18 symbols. Component behaviour
comes from the parts' own datasheets (ST7735S panel spec
`N096-1608TBBIG09-C08`, Diodes PAM8904, Semtech SX1262).

| Function | nRF52840 pin | Schematic net | ZephCore mapping |
| --- | --- | --- | --- |
| LoRa SCK | P1.13 | LOAR_SCK | `spi2` SCK |
| LoRa MOSI | P1.14 | LOAR_MOSI | `spi2` MOSI |
| LoRa MISO | P0.03 | LOAR_MISO | `spi2` MISO |
| LoRa NSS / CS | P1.11 | LOAR_NSS | `cs-gpios` |
| LoRa reset | P0.02 | LOAR_NRSET | `reset-gpios` |
| LoRa busy | P0.29 | LORA_BUSY | `busy-gpios` |
| LoRa DIO1 | P0.31 | DIO1 | `dio1-gpios` |
| Antenna switch | SX1262 DIO2 | ANT_SW_CTRL | `dio2-tx-enable` |
| GNSS UART TX from MCU | P0.07 | GNSS_RX | `uart0` TX |
| GNSS UART RX into MCU | P0.08 | GNSS_TX | `uart0` RX |
| GNSS reset | P0.26 | GNSS_RST | `gps-reset` alias, active-low |
| GNSS rail enable | P0.04 | VGNSS_Ctrl | regulator `gps_power`, active-low |
| GNSS PPS | P1.09 | PPS | documented, not consumed |
| Display SCK | P1.00 | SCLK | `spi3` SCK |
| Display MOSI | P0.24 | SDIN | `spi3` MOSI |
| Display CS | P0.12 | CS | `spi3` `cs-gpios` |
| Display DC | P0.22 | RS | MIPI-DBI `dc-gpios` |
| Display reset | P0.20 | RES | MIPI-DBI `reset-gpios` |
| Display backlight | P0.15 | LED_K | regulator `disp_pwr_enable`, active-low |
| Display panel rail | P0.13 | VScreen_Ctrl | regulator `tft_pwr_enable`, active-low |
| Buzzer tone | P0.09 | DIN | `pwm0` channel 0, `buzzer` alias |
| Buzzer 2× select | P1.02 | EN1 | unclaimed (10K pull-up) |
| Buzzer 3× select | P1.05 | EN2 | unclaimed (10K pull-up) |
| White LED | P0.16 | LED | `led0`, `lora-tx-led`, active-low |
| User button | P1.10 | Button | `sw0`, active-low |
| Function button | P0.14 | User | `KEY_LEFT`, active-low |
| Battery ADC | P0.05 / AIN3 | BAT_ADC | ADC channel 3 |
| Battery ADC enable | P0.11 | ADC_Ctrl | regulator `vbat_enable`, active-high |
| Sensor rail enable | P1.06 | VSensor_Ctrl | regulator `sensor_power`, active-low, held off |
| Sensor I2C SDA | P1.03 | Sensor_SDA | not enabled |
| Sensor I2C SCL | P0.10 | Sensor_SCL | not enabled |
| IMU INT1 / INT2 | P1.01 / P1.07 | INT1 / INT2 | not consumed |

The power button (S1) is not visible to firmware. It feeds the EC190708 latch
that gates the 3V3 LDO enable and, through Q7/Q4, pulls `nRF_RST` low. Per the
datasheet: 3 s hold = on/off, short press = reboot, double-click = DFU.

## Two Heltec document errors, corrected here

**The white LED is on P0.16, not P0.09.** Datasheet §2.2 says "White Function
LED, connected to P0.09". P0.09 is the PAM8904 `DIN` (buzzer). The schematic
puts the LED on P0.16, and Heltec's own nRF52 Arduino variant
(`HT-mesh-node-t1`) agrees.

**The GNSS UART aliases in the schematic's pin table are inverted.** The table
prints `nRF_RX` beside P0.07 and `nRF_TX` beside P0.08. The `GNSS_RX`/`GNSS_TX`
halves of the same rows are correct, and they are the ones to trust:

- `GNSS_RX` lands on UC6580 pin 18, whose symbol pin name is `RX` — an input.
  Nothing else drives that node, so P0.07 can only be the nRF's **transmit**
  line. Two inputs tied together with no driver is not a circuit.
- `GNSS_TX` lands on UC6580 pin 19 (`TX`, an output), which must feed an MCU
  input, so P0.08 is the nRF's **receive** line.

Heltec's own Arduino core (`HelTecAutomation/Heltec_nRF52`,
`variants/HT-mesh-node-t1/variant.h`) sets `PIN_SERIAL1_TX = P0.07` and
`PIN_SERIAL1_RX = P0.08` — the same way round — and it is what their shipped
`GPS_To_Serial` test firmware for this board is built against. Meshtastic's
variant is a copy of it and agrees.

Arduino MeshCore's `variants/heltec_t1/variant.h` has these two swapped
(`PIN_SERIAL1_RX = P0.07`), still on both `main` and `dev`. Their T1 GPS
cannot be receiving.

If a first hardware run shows no NMEA, swapping the two `psels` in
`heltec_t1_nrf52840-pinctrl.dtsi` is the one-line test.

## Display

- Controller: ST7735S, driven by Zephyr's `sitronix,st7735r` driver.
- Panel: Unvision `N096-1608TBBIG09-C08`, 0.96", 80(H)RGB × 160(V), dot pitch
  0.135 mm, active area 10.8 × 21.7 mm, normally black, 4-line SPI, one white
  LED backlight, −20…+70 °C.
- Bus: SPI3 through MIPI-DBI, **write-only** — the 8-pin FFC has no data-out
  line.
- UI path: `zephcore,mono-tft` wraps the colour TFT as a 1bpp display for
  ZephCore's CFB UI. At 160×80 with the 6×8 font that is 26 columns × 10 rows,
  identical to the T096 and the Heltec Wireless Tracker.
- Orientation: landscape, rotated 180° from the T096 mounting of the same panel
  class — `madctl = 0x68` (MX | MV | BGR) against `0xa8` (MY | MV | BGR) there.
  Offsets are `x-offset = 0`, `y-offset = 24`, matching the "red tab 160×80"
  column-start of 24 / row-start of 0 transposed by MV.

  **To check on first boot:** mirroring an 80-column window that sits at GRAM
  offset 24 inside the ST7735S's 132 columns should geometrically land it at
  28, yet both mainstream Arduino ST7735 drivers keep the same offsets across
  rotations 1 and 3. If the image is shifted with a black band on one edge, the
  mirrored alternative is `x-offset = 2`, `y-offset = 28`,
  `caset = [00 02 00 a1]`, `raset = [00 1c 00 6b]`.
- Power: the panel rail (`VScreen_Ctrl`) is boot-enabled so the ST7735 init
  sequence reaches a powered panel. The backlight FET's source is `VDD_3V3`,
  **not** the panel rail, so it has to be switched off in its own right —
  `disp_pwr_enable` does that and follows ZephCore display on/off.

## Buzzer

The piezo is driven by a PAM8904 charge pump, not a bare transistor. From the
MCU's side it is still one tone input: PWM0 channel 0 on P0.09.

Its charge-pump multiplier is selected by EN1 (P1.02) and EN2 (P1.05), which
both carry 10K pull-ups to VDD_3V3. Per the PAM8904 datasheet that is 3× mode —
7.2–9 V out, the loudest setting — with no firmware involvement, and the part
sits under 1 µA whenever `DIN` is low, auto-sleeping 42 ms after the tone
stops. So both pins are deliberately left unclaimed. Driving them is only
worth doing if a quieter mode is ever wanted:

| DIN | EN1 | EN2 | Mode |
| --- | --- | --- | --- |
| 0 | × | × | shutdown |
| 1 | 0 | 0 | shutdown |
| 1 | 0 | 1 | 1× (2.8–3 V) |
| 1 | 1 | 0 | 2× (5.2–6 V) |
| 1 | 1 | 1 | 3× (7.2–9 V) |

## Buttons

Two buttons, so unlike the single-button Heltec boards the T1 can page
backwards.

| Button | Pin | Gesture | Action |
| --- | --- | --- | --- |
| User (S3) | P1.10 | 1 tap | page next |
| | | 2 taps | LED heartbeat toggle |
| | | 3 taps | notification mode |
| | | 4 taps | GPS on/off |
| | | 5 taps | flood advert |
| | | hold 1 s | page enter |
| Function (S2) | P0.14 | press | page previous |

The datasheet calls P1.10 "Button1 / USER key" and P0.14 "Button2 / Function
key"; the schematic silkscreen labels are the other way round (S2 = "User1" on
P0.14, S3 = "User2" on P1.10). Pins are what matter.

## NFC pins used as GPIO

P0.09 (buzzer `DIN`) and P0.10 (`Sensor_SCL`) are the nRF52840's NFC antenna
pins, so `UICR.NFCPINS` has to select GPIO. The DTS sets `nfct-pins-as-gpios`
on `&uicr`.

Stock boards already have this programmed by the factory Adafruit UF2
bootloader, so a normal UF2 install needs nothing extra. A board that has been
fully erased over SWD does, and that property is what puts it back.

## Not enabled

- **Sensor I2C bus.** The ICM-42607-P and MMC5983MA sit on `Sensor_SDA`
  (P1.03) / `Sensor_SCL` (P0.10) behind the `VSensor_Ctrl` rail, which also
  powers the bus's 4.7K pull-ups. ZephCore has no consumer for either part —
  there is no IMU support, and the MMC5983MA has no Zephyr driver at all — and
  the board is sealed with no external sensor connector, so the bus stays down
  and the `sensor_power` regulator node exists purely to hold P1.06 inactive.
  The datasheet's 11 µA sleep figure assumes that rail is off.

  To bring it up: enable `&i2c0` with a pinctrl pair for those two pins and add
  `regulator-boot-on` to `sensor_power`.
- **U6**, an 8-pin pressure-sensor footprint on the same bus, is marked NC on
  the schematic, and so is `R25`, its SDO/address strap — the address would
  float even if a part were fitted.
- **External flash.** There is none on this board, so `&qspi` is disabled and
  `/ext` is unavailable. Contacts and channels live in internal `/lfs`.

## Heltec license page overlap

Heltec's own Arduino variant reserves `0xF3000`–`0xF4000` (the last flash page
before the bootloader) for a hardware-version byte and a license blob
(`HARD_VERSION_ADDR` / `HT_LICENSE_ADDR`). That page falls inside ZephCore's
`/lfs` partition, so running ZephCore will eventually erase it, the same way it
does on the other nRF52 boards that overlap an Adafruit-era filesystem region.
It only matters if you intend to go back to Heltec's own F&T / SnapEmu
firmware, which is what reads that blob; MeshCore and Meshtastic do not.

## Hardware smoke test plan

1. Flash UF2 by double-clicking the power button and drag-dropping.
2. Confirm USB CDC/logging appears.
3. Confirm the white LED goes out after boot (it is lit from power-up until
   firmware drives P0.16 high).
4. Confirm BLE advertises and the MeshCore app pairs.
5. Confirm the display lights and shows the companion UI. **Check the image
   fills the panel edge to edge** — see the offsets note above.
6. Confirm the backlight and panel both go dark on display timeout.
7. Confirm both buttons: page next / page previous.
8. Confirm SX1262 init with no reset, busy or DIO1 errors, then RX.
9. Test TX at low power first, then step up — the antenna switch is chip-driven
   via DIO2, so a wrong `dio2-tx-enable` shows up as very poor range rather
   than an error.
10. Confirm the buzzer sounds.
11. Confirm GNSS last: `get gps` should show sentences arriving. **If nothing
    arrives, swap the UART psels** — see the GNSS UART note above.
