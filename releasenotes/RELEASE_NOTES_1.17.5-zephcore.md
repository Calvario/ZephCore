# ZephCore 1.17.5-zephcore

> [!NOTE]
> **Draft — release in progress.** Covers what is on `dev` so far.

---

## Bluetooth range on ESP32 boards was 9 dB down

Every ESP32 board has been advertising and connecting at 0 dBm; stock MeshCore runs the same radios
at +9 dBm. Zephyr's driver builds its default from the `CONFIG_BT_CTLR_TX_PWR_*` chain, and with none
of them set it fell through to the 0 dB arm — silently, with nothing visible at build time or on the
node. ESP32 boards now transmit at +9 dBm.

> [!NOTE]
> **Nothing to change on your side.** No setting, no re-pairing. nRF52, nRF54L and MG24 were never
> affected — they set their own transmit power.

---

## GPS on Heltec WiFi LoRa 32 V4 and V4.3

Both boards bring a GPS UART and its power pins out to the header, and ZephCore described neither, so
attaching a module did nothing. They now match every other GPS board: NMEA on UART1 at 9600 baud, with
enable and reset driven rather than left floating.

> [!NOTE]
> **For an external module.** Neither board ships with a receiver; boards with nothing attached are
> unaffected.

---

## The confirmation prompt now works on single-button boards

Shutdown, DFU and off-grid confirm with two ENTER presses. On single-button boards ENTER is a
one-second hold, and the window was half a second — so the second hold always landed too late and
re-armed the prompt instead of confirming. The window is now three seconds on every board that emits
ENTER through a long-press filter. Six had it; eight did not:

| | |
|---|---|
| Heltec WiFi LoRa 32 V3 | Heltec Wireless Tracker |
| Heltec WiFi LoRa 32 V4 | Heltec Wireless Tracker V2 |
| Heltec WiFi LoRa 32 V4.3 | LilyGo T3-S3 |
| Meshnology W12 | TTGO T-Beam |

Joystick boards (Wio Tracker L1, GAT562) use a different menu and were never affected.

Thanks to **bisbille** for finding this and fixing the first two boards.

---

## Changing frequency or spreading factor now resets adaptive CAD

The learned CAD threshold is an offset from a per-SF, per-bandwidth base. Change the preset and the
base moves, but the node kept the old offset — leaving it too sensitive to transmit or too deaf to
defer. `set radio`, `set freq` and the app's radio settings now perform a full `set cad.reset` when
frequency, bandwidth or spreading factor changes. Coding rate is excluded: it changes airtime, not the
threshold. `tempradio` uses its preset's own base and hands the offset back on revert without writing
flash; `get cad` shows `a:tmp` while a window is open.

> [!NOTE]
> **Nothing to change on your side.** The reset is automatic and the node re-converges in an hour or
> two. `set cad.reset` by hand is no longer needed after a preset change.

Thanks to **Codes** for reporting it.

---

## ProMicro SX1262: Bluetooth dropped every few seconds, and the screen never worked

The nRF52840 SuperMini has no 32.768 kHz crystal, but the board was configured as though it did — so
the Bluetooth controller and the kernel tick ran off a floating oscillator while claiming 50 ppm
accuracy, and the link dropped every few seconds. It now uses the calibrated internal RC at 250 ppm,
matching stock MeshCore's settings for the same silicon. Timing that hangs off the same clock steadies
with it, including the return to receive after a transmission — so repeats of your own message, and
zero-hop ping replies, are no longer missed.

The OLED was never described for this board at all. It is now an SSD1306 on the same I2C pins stock
MeshCore uses, optional at runtime.

That display also needed a second fix, found while bringing up the LR2021 EVK below, which had the
identical fault. On these Nordic chips the screen is redrawn in one large transfer, and the driver
needs somewhere to assemble it; the space reserved for that defaults to 16 bytes, far short of the
1025 a 128x64 screen needs. The panel answered every setup command and then never drew anything —
which looks exactly like a broken display rather than a misconfigured one. Both boards now reserve
enough.

> [!NOTE]
> **The ProMicro half of this is unconfirmed on hardware.** The fault was identified from the LR2021
> EVK's logs and the fix is the same single line, but nobody here has a ProMicro with a screen
> attached to check. If yours has an OLED, we would like to hear either way.

> [!NOTE]
> **Check transmit power if your module has no amplifier.** The 10 dBm default is the safe drive level
> for an E22-900M30S. On a bare module (HT-RA62, E22-900M22S) that is your antenna power — 12 dB under
> stock — which reads as no repeats and failed zero-hop pings. `set tx 22` once and it sticks.

Thanks to **Mike's Allotment** for the report.

---

## New board: Semtech LR2021 LoRa Plus Evaluation Kit

The **Semtech LR2021 LoRa Plus EVK** is now supported — Seeed's kit built around Semtech's
fourth-generation LoRa transceiver. It is three boards stacked: a XIAO nRF54L15 for the processor, the
LoRa Plus expansion board for the display, buttons, Grove ports and antenna sockets, and a Wio-LR2021
radio module. Companion and repeater builds are both provided, and the 128x64 OLED and the expansion
board's user button work as they do on any other screen-equipped board.

Build it with `seeed_lr2021_evk/nrf54l15/cpuapp`. It is a separate board from the plain **XIAO
nRF54L15**, which is the same processor on a Wio-SX1262 carrier — the two are wired differently and
the firmware is not interchangeable.

> [!IMPORTANT]
> **Two things on the hardware to check before first power-on.** The small two-pin **IDCC** header
> feeds power to the radio module; if its jumper is missing the radio is simply unpowered and looks
> dead. And the radio module connects to the board's SMA sockets through **U.FL pigtails you fit
> yourself** — connect the sub-GHz (LF) one before transmitting. Transmitting at full power into an
> unconnected antenna port can damage the amplifier.

> [!NOTE]
> **This board can only be flashed with SWD.** The nRF54L15 has no USB hardware, so there is no
> drag-and-drop UF2 and no update over a cable. The expansion board's USB-C socket reaches a SAMD11
> debug bridge, which is enough on its own — no separate probe needed — and the same socket carries the
> console at 115200 baud. The firmware is published as a `.hex` file, and the Mesh America configurator
> lists the board as a download rather than offering to flash it.

The kit's 2.4 GHz antenna port is not used. MeshCore is a sub-GHz protocol, so only the LF port carries
traffic.

---

## Also in this release

*To be filled in as further changes land.*
