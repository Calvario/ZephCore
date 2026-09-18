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

## LR2021 boards can now use the 2.4 GHz band

The LR2021 has two radio front ends — the sub-GHz one everything has always used, and a second covering
1.9–2.5 GHz. ZephCore only ever drove the first. Setting a 2.4 GHz frequency was accepted and then
quietly transmitted down the sub-GHz path into a sub-GHz antenna, which radiates essentially nothing.

The band is now chosen automatically from the frequency. At or above 1500 MHz the driver switches to the
high-band amplifier, the high-band receive path and high-band calibration; below it, nothing changes from
before. The amplifier settings come from Semtech's own published measurements for each path.

Transmit power follows the band, because the two paths have different ceilings: **+22 dBm below
1500 MHz, +12 dBm above it.** A node carrying a sub-GHz power setting into the 2.4 GHz band is turned
down to 12 rather than being asked for something the hardware cannot do.

The wider channels that 2.4 GHz LoRa normally runs on — 203, 406, 812 and 1000 kHz — are available too,
on LR2021 boards only. Type either the round number or the exact one. Every other radio ZephCore supports
silently falls back to 125 kHz when handed a channel width it does not implement, so those boards still
stop at 500 and say so.

> [!IMPORTANT]
> **This is a separate network, not a bridge.** Both ends of a link must be on the same band; a 2.4 GHz
> node cannot hear sub-GHz traffic or be heard by it. Range is also far shorter than sub-GHz at the same
> power. Treat it as something to experiment with rather than a drop-in upgrade.

> [!NOTE]
> **Connect the right antenna.** On the LR2021 LoRa Plus EVK the two bands leave through different
> sockets — 2.4 GHz uses the HF port. On a board with only a sub-GHz antenna, 2.4 GHz has nowhere to go.

A first attempt at this shipped with the setters widened but the *loaders* left alone, so a 2.4 GHz
frequency was accepted, saved, and then thrown away on the next boot — the node came back on factory
defaults. The accepted ranges now live in one place shared by the USB CLI, the phone app's protocol,
the observer CLI and both preference loaders, so a setting that is accepted is a setting that survives
a reboot.

> [!NOTE]
> **1625 kHz is not an LR2021 bandwidth.** Some apps list it alongside the 2.4 GHz channel widths
> because it exists on older Semtech 2.4 GHz parts. This chip stops at 1000 kHz, so picking 1625 is
> refused rather than quietly run at something else.

> [!NOTE]
> **"Client repeat" stays sub-GHz.** That option is restricted to 433, 869.495 and 918 MHz for
> regulatory reasons, so turning it on while tuned to 2.4 GHz is refused. Ordinary companion and
> repeater operation is unaffected.

> [!NOTE]
> **Not yet tested on air.** The band switching follows Semtech's reference implementation and the
> amplifier tables are their published measurements, but no 2.4 GHz link has been run here yet. Reports
> welcome.

---

## Also in this release

*To be filled in as further changes land.*
