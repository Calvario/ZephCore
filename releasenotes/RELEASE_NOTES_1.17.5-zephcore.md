# ZephCore 1.17.5-zephcore

> [!NOTE]
> **Draft — release in progress.** Covers what is on `dev` so far. More entries will land before this ships.

A Bluetooth range fix that affects every ESP32 board, GPS support on the two Heltec V4 boards, and the
single-button confirmation prompt now works on the eight boards where it never could.

---

## Bluetooth range on ESP32 boards was 9 dB down

Every ESP32 board — S3, C-series and classic alike — has been advertising and connecting at 0 dBm.
Stock Arduino MeshCore runs the same radios at +9 dBm. That is the reported "ZephCore has worse
Bluetooth range than the official firmware" on Heltec V4, and the gap is real: 9 dB is roughly 2.8
times the distance in free space.

Nothing was misconfigured. The setting was deliberately left alone, on the understanding that the
Espressif controller blob picked its own transmit power. It does not. Zephyr's driver hands the
controller a default built from a chain of `CONFIG_BT_CTLR_TX_PWR_*` options, and with none of them
set the chain fell through to its `use 0dB TX power as default` arm. Silently, with no warning at
build time and nothing visible on the node.

ESP32 boards now transmit at +9 dBm, matching stock MeshCore. That is comfortably inside the EU
2.4 GHz 100 mW limit, and it is the one level every Espressif chip family in the tree supports, so it
applies everywhere without per-board overrides.

> [!NOTE]
> **Nothing to change on your side.** No setting, no re-pairing. Flash and the range is there.
> nRF52, nRF54L and MG24 boards were never affected — they set their own transmit power already.

---

## GPS on Heltec WiFi LoRa 32 V4 and V4.3

Both boards bring a GPS UART and its power-control pins out to the header, and ZephCore had no
wiring for either. Attaching a module did nothing.

They are now described the same way every other GPS board is: NMEA on UART1 at 9600 baud, with the
enable and reset lines driven properly rather than left floating into the module. `set gps on` and
the GPS pages behave as they do elsewhere.

> [!NOTE]
> **This is for an external module.** Neither board ships with a GPS receiver — the pins are there for
> one you add yourself. Boards with nothing attached are unaffected.

---

## The confirmation prompt now works on single-button boards

Shutdown, DFU and off-grid all ask you to confirm by pressing ENTER twice. On boards with one button,
ENTER is not a tap — it is a one-second hold, produced by a long-press filter.

Two one-second holds cannot fit inside a half-second window, and half a second is what the window
was. The second hold always arrived after it had closed, so instead of confirming, it re-armed the
prompt. Pressing again did the same thing. On these boards the three actions simply could not be
confirmed from the screen, however many times you tried.

The window is now three seconds on every board that emits ENTER through a long-press filter, which is
what the setting's own documentation had been recommending all along. Six boards had it; eight did not.

Newly fixed:

| | |
|---|---|
| Heltec WiFi LoRa 32 V3 | Heltec Wireless Tracker |
| Heltec WiFi LoRa 32 V4 | Heltec Wireless Tracker V2 |
| Heltec WiFi LoRa 32 V4.3 | LilyGo T3-S3 |
| Meshnology W12 | TTGO T-Beam |

> [!NOTE]
> **Boards with a joystick were never affected.** The Wio Tracker L1 and GAT562 use a different menu,
> which has always allowed three seconds. Nothing changes for them.

Thanks to **bisbille** for finding this and fixing the first two boards.

---

## Changing frequency or spreading factor now resets adaptive CAD

Adaptive CAD learns a listen-before-talk threshold as an offset from a base value the chip family
publishes per spreading factor and per bandwidth. Change SF or bandwidth and that base moves under
the offset — but the node kept the old offset and, at the next boot, shifted it further to preserve
the absolute threshold it used to name. Correct after a firmware table change, wrong after a preset
change, where the base table's own step is the physics. On an LR1110 the SF7-to-SF12 step alone is
16 counts, enough to slam the offset to its rail: too sensitive and the node defers transmitting on
noise, or too deaf and it transmits over live receptions.

`set radio`, `set freq` and the app's radio settings now perform a full `set cad.reset` whenever they
move frequency, bandwidth or spreading factor — the learned offset and the probe statistics behind it
both belong to the preset you just left. Coding rate is excluded; it changes airtime, not the
threshold. `tempradio` visits its preset at that preset's own base and hands the learned offset back
on revert, without ever writing it to flash; `get cad` shows `a:tmp` while a window is open.

> [!NOTE]
> **Nothing to change on your side.** The reset is automatic, and the node re-converges in an hour or
> two. Reaching for `set cad.reset` by hand after a preset change is no longer necessary.

Thanks to **Codes** for reporting it.

---

## Also in this release

*To be filled in as further changes land.*
