# Supported Boards

Board strings for use with `west build -b <board> zephcore`.

## nRF52840

```
rak4631
rak3401_1watt
wio_tracker_l1
t1000_e
meshtracker_x1
thinknode_m1
thinknode_m3
thinknode_m6
rak_wismesh_tag
ikoka_nano_30dbm
sensecap_solar
xiao_nrf52840
lilygo_techo
promicro_sx1262
heltec_t114
heltec_t096
gat562_30s
muziworks_r1neo
```

> **RAK WisMesh Pocket** (WisBlock pocket): use `-b rak4631` — same board string and firmware as **RAK4631**.
>
> **Heltec T114** screenless build: append `boards/nrf52840/heltec_t114/no_display.conf` to `EXTRA_CONF_FILE` for units without the TFT module.
>
> **SenseCAP MeshTracker X1** (`meshtracker_x1`): nRF52840 + LR2021, AG3335M dual-band L1+L5 GNSS, SPA06 barometer, DRV2605L vibration, YSN8900 RTC, 8 MB QSPI flash (`/ext`), RGB LED, buzzer. Untested on hardware — first ZephCore board to use a real LR2021. The RTC is treated as an RX8900 second-source; boot-time discovery validates that before trusting it.
>
> **Heltec Mesh Node T096** (`heltec_t096`): nRF52840 with SX1262 + KCT8103L PA/FEM, UC6580 GNSS, and ST7735S 160x80 TFT companion display. The external SPI flash footprint is documented in the board notes but left disabled until the device parameters are confirmed.

## ESP32

```
xiao_esp32c3
xiao_esp32c6/esp32c6/hpcore
xiao_esp32s3/esp32s3/procpu
lilygo_tlora_c6/esp32c6/hpcore
lilygo_t3s3/esp32s3/procpu
station_g2/esp32s3/procpu
heltec_wifi_lora32_v3/esp32s3/procpu
heltec_wifi_lora32_v4/esp32s3/procpu
heltec_wifi_lora32_v43/esp32s3/procpu
heltec_wireless_tracker/esp32s3/procpu
heltec_wireless_tracker_v2/esp32s3/procpu
thinknode_m9/esp32s3/procpu
ttgo_tbeam/esp32/procpu
ttgo_lora32/esp32/procpu   # source-only, no published firmware
```

> ESP32 boards require `west blobs fetch hal_espressif` before first build.
>
> Heltec V3 console/shell use `uart0` (UART serial) in ZephCore.
>
> **Heltec Wireless Tracker** (`heltec_wireless_tracker/esp32s3/procpu`): V1.1
> ESP32-S3-FN8 companion with SX1262, ST7735R 160x80 TFT, and UC6580 GPS.
>
> **Heltec Wireless Tracker V2** (`heltec_wireless_tracker_v2/esp32s3/procpu`):
> ESP32-S3FN8 companion with SX1262 + KCT8103L PA/FEM, ST7735R 160x80 TFT,
> UC6580 GNSS, battery ADC, and USB-C native serial/JTAG.
>
> **ThinkNode M9** (`thinknode_m9/esp32s3/procpu`): ESP32-S3 (16MB flash, 8MB
> PSRAM) handheld with LR1110, ST7789 320x240 TFT, CC1167Q GPS, PCF8563 RTC,
> and buzzer. Not yet validated on hardware — see the board README for the
> bring-up list. The onboard STC8H I2C keyboard, IMU/magnetometer, and SD slot
> are not ported. The LR1110 radio-firmware updater supports this board
> (`west build -b thinknode_m9/esp32s3/procpu zephcore/tools/lr1110_updater`).
>
> **LilyGo T-Beam** (`ttgo_tbeam/esp32/procpu`): classic ESP32 (PICO-D4) with
> SX1262, AXP2101 PMU, and GNSS. Use this for the **v1.2 SX1262** variant — the
> upstream Zephyr DTS models the SX1276 radio, which ZephCore overrides to
> SX1262 in `board.overlay`. Console/CLI are on `uart0` (onboard USB-UART).
>
> **TTGO LoRa32** (`ttgo_lora32/esp32/procpu`): classic ESP32 (PICO-D4) with
> **SX1276**. Console/CLI on `uart0`.
>
> **Source-only — no published firmware, and not a supported configuration.**
> It is deliberately absent from `build.sh`, the release workflow and the Mesh
> America catalog, so no release ever carries a binary for it. It is kept in the
> tree as the one exercise of the SX127x loramac-node radio path, which is old,
> lacks RX duty cycle and RX gain boost, and gets none of the work the native
> SX126x/LR11xx/LR20xx drivers do. Build it yourself if you want it; expect to
> maintain it yourself too.
>
> If you do build the companion, note it is DRAM-bound (~97% of `dram0_0_seg`)
> and capped at **50 contacts / 32 queued offline messages** — lower than any
> other companion board. The repeater has no such limit. At the 150/128 this
> board's config used to declare, it did not link at all. See `board.conf` for
> the measured budget before raising either number.

## STM32WL

```
lora_e5_mini
```

> **Seeed LoRa-E5 mini** (`lora_e5_mini`): STM32WLE5JC with the integrated
> SX1262-class sub-GHz radio. No BLE and no USB device — the companion
> protocol and the CLI both run over USART1 (bridged to USB-C by the onboard
> USB-UART chip). Flash over SWD/ST-Link with `west flash`.

## MG24 (Silicon Labs)

```
xiao_mg24
```

> Requires `west blobs fetch hal_silabs` and pyocd.

## nRF54L

```
xiao_nrf54l15/nrf54l15/cpuapp
me25ls02/nrf54l15/cpuapp
```

> Requires `--no-sysbuild` flag: `west build -b xiao_nrf54l15/nrf54l15/cpuapp zephcore --no-sysbuild`
>
> The nRF54L15 has no USB peripheral, so there is no UF2 or DFU path on either board — flash
> `zephyr.hex` over SWD. `me25ls02` is the MinewSemi ME25LS02 module on its MX25LE02 carrier;
> its USB-C port is a CH340x UART bridge carrying the console, not a programming port.
> Untested on hardware.

## Native Linux

Not `west build -b` boards — `EXTRA_CONF_FILE` presets on top of `native_sim`
for SBCs (Femtofox / Luckfox Pico Mini, Raspberry Pi + RAK6421 HAT). See
[LINUX_NATIVE.md](../LINUX_NATIVE.md) for build commands and wiring.
