# ZephCore 1.17.5b-zephcore

Bluetooth range on ESP32 boards goes back up to stock MeshCore's +9 dBm. Several GPS problems are
fixed: control lines driven backwards on a handful of boards, and repeaters waking their GPS every
five minutes instead of every two days. The ProMicro SX1262's Bluetooth stops dropping, LR1110 boards
stop occasionally missing a packet with duty cycle on, and changing the radio preset now resets
adaptive CAD. There are four new boards, including Semtech's LR2021 evaluation kit, and the first
2.4 GHz support. Underneath, the firmware moves back onto Zephyr's main branch.

> [!NOTE]
> A normal upgrade keeps your identity, settings, contacts and phone pairing. Nothing needs redoing.

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

## GPS control lines were driven backwards on several boards

A GPS module has up to three control lines: power enable, reset, and on some boards a second reset or
a sleep line. Some boards wire them "active-low" (the line is switched on by pulling it down). The
GPS code set these lines up in one convention and then drove them in the other, so on an active-low
line the first command did the opposite of every command after it.

What that meant on the boards that shipped:

- **Meshnology W12:** turning the node off left the GPS powered, so a node in System OFF kept a GPS
  module running.
- **Heltec T096, Heltec T114 and ThinkNode M1:** the reset pulse at GPS power-on never actually
  happened, so the module started uncontrolled rather than from a clean reset.

Every GPS control line is now driven one consistent way, and reset is held on while the node is off,
as stock MeshCore does. Boards whose lines are all active-high see no change at all. The new Heltec
V4 GPS support above, the V4-R8 and the Heltec Mesh Node T1 are built on the corrected code.

Thanks to **ptr727** for finding and fixing this.

---

## GPS repeaters woke their GPS every 5 minutes instead of every 48 hours

Repeaters and room servers applied the GPS interval before they had read their settings, so they
always got the companion default of 5 minutes: about 576 times more GPS wake-ups than the 48-hour
repeater default. On a battery or solar node that is a real power cost. For the same reason
`set gps duty` was saved but never reached the GPS, and `get gps duty` answered 300 whatever you had
set.

> [!NOTE]
> **Nothing to change on your side.** The saved setting now takes effect on the next boot. If you ever
> set `gps duty` on a repeater and thought it had not worked, it will now.

Thanks to **mepholic** for finding and fixing this.

---

## LR1110 boards with RX duty cycle could miss a packet

On the T1000-E and ThinkNode M3, with RX duty cycle on, the node could occasionally miss a packet
that arrived shortly after it had finished processing the previous one. With duty cycle on, the radio
driver cannot see the start of an incoming packet, so the periodic noise-floor measurement could stop
the radio in the middle of one. The driver now checks the chip first and puts the measurement off
while a packet is arriving. On a bench T1000-E, adverts sent 1.2 s apart went from 198 of 200
received to 200 of 200; 600 of 600 across all timings tested.

SX1262 boards were never affected, because the same measurement does not interrupt their radio.
LR2021 boards (MeshTracker X1) get the same fix, but it has not been run on one yet.

---

## ZephCore now builds on Zephyr's main branch again

Since June the firmware tracked Zephyr's 4.4 stabilisation branch. This release moves it back to
Zephyr's main line, catching up about 16,000 upstream changes in one step. Almost all of that is
invisible from the outside. What you might notice:

- **Nothing to redo.** Storage formats are unchanged, so identity, settings, contacts and channels
  survive the update, and phone pairings carry over. Flash layouts are unchanged on every board.
- **ESP32 boards:** flash `-merged.bin` exactly as before. The bootloader still accepts the same kind
  of image and sits where it always has; it is actually a little smaller than before.
- **Memory.** ESP32-S3 and ESP32-C3 boards lost about 20 KB of RAM to a larger Bluetooth controller
  from Espressif. Right-sizing the memory pool it allocates from, against measurements taken during a
  live phone session, wins almost all of it back: S3 companions end up within about a point of 1.17.4,
  and the XIAO ESP32-C3 slightly better off. Every board also gets 4 KB back from a background stack
  that was many times larger than it ever needed, which is a straight gain on nRF52, nRF54L and MG24.

A few older problems turned up during the move and are fixed:

- **ESP32-S3 `start dfu` killed the USB port until a power cycle** on repeater, room server, observer
  and debug builds, the ones that use the USB port as a console. It now comes back in download mode
  as it should.
- **A USB companion could keep the processor permanently busy** after a command over USB, starving
  the radio's transmit handling. It never showed up on the previous Zephyr, but the code was there;
  it is fixed rather than left to surface later.

> [!NOTE]
> **WiFi OTA across this update has not been tested.** The image format is the same kind as before,
> but flashing `-merged.bin` over USB is the path that was tested. If you use WiFi OTA on an ESP32
> repeater, do one board first.

---

## XIAO nRF54L15: battery reading, and a flashing fix

The XIAO nRF54L15 now reports battery voltage. The board has a divider and enable switch for it; they
were just never wired into the firmware. The scaling was measured on hardware against the chip's own
supply rails, and `set adc.multiplier target <mV>` trims it per board as usual.

Separately, `west flash` could leave the last few bytes of the image from whatever firmware was there
before, which caused random crashes at boot that looked like hardware faults. Zephyr's flashing script
was missing a final write step; that is now fixed upstream and included. It only affected people
flashing their own builds with `west flash`, not other SWD tools. A XIAO nRF54L15 flashed this way has
been checked end to end: full image verified, clean reboot, and 50 of 50 packets both ways against a
T1000-E with and without RX duty cycle.

---

## New boards

- **Heltec Mesh Node T1** (`heltec_t1`): nRF52840 + SX1262 tracker card with a UC6580 L1+L5 GNSS,
  a 160x80 colour screen, a piezo buzzer, two buttons and an 1850 mAh battery. Companion and repeater
  builds. **Not yet tested on hardware**; if you have one, the GPS and the screen position are the two
  things we would most like to hear about.
- **Wio Tracker L1 Pro 1W** (`wio_tracker_l1_1w`): the Wio Tracker L1 with a 1 W amplifier. Its pin
  map comes from Meshtastic's variant, not MeshCore's: MeshCore's drives the board's power-detector
  pin as a receive switch, which this board does not have. **Not yet tested on hardware.** It ships at
  the chip's full 22 dBm into the amplifier, as both upstreams do; nobody has measured whether the
  amplifier wants less.
- **Heltec WiFi LoRa 32 V4-R8** (`heltec_wifi_lora32_v4_r8`): the V4 with 8 MB of octal PSRAM, which
  moves several control pins. It also has the V4.3's front end, so `set radio.fem.rxgain` works on it.
  In the Mesh America configurator it sits under the Heltec V4 tile as the "R8" entries.

> [!WARNING]
> **Do not flash V4 or V4.3 firmware onto a V4-R8.** It sets the memory up wrongly and drives pins
> that belong to the PSRAM on this part; the screen and sensors never power on.

Thanks to **ptr727** for the V4-R8 port.

---

## Also in this release (via west update, upstream sync or manually added)

- **The restart notice says why, in plain English.** The companion's `v` contact reports why the
  node restarted, e.g. `PIN(reset button)`, `BROWNOUT(low voltage)`, `WATCHDOG(hang)`. Every role
  now records the reset cause at boot and clears it, where before only companions did, so a repeater's
  watchdog reset is logged instead of silently carried into a later boot. Thanks to **ptr727**.
- **Replies to the app were occasionally lost.** If BLE or USB could not take a reply at that moment,
  the reply was dropped and the app waited for an answer that never came. The companion now keeps the
  reply and pauses reading new commands until it has gone out. The Linux TCP transport also closes a
  connection that announces an oversized frame instead of trying to read it.
- **Airtime estimates were wrong at SF5 and SF6, and on some LR1110 settings.** They feed the
  duty-cycle budget and the transmit watchdog. SF5/6 were charged about 1.7 times their real airtime,
  and LR1110 boards underestimated SF10 at 62.5 kHz and SF12 at 250 kHz. The LR1110 estimate now uses
  Semtech's own calculation.
- **A channel message too long for the radio got the wrong error.** It came back as "queue full, try
  later" for a send that could never succeed; it is now refused as invalid.
- **`set dutycycle` now goes down to 10%, not 1%.** Anything under 10% was silently turned into 10% at
  the next boot, so the node ran at one duty cycle and woke up at another. `set af` is now limited to
  0–9: out-of-range values could divide by zero until the next reboot.
- **A packet returned to the pool twice** is now reported, instead of silently losing a slot.
- **The Meshnology W12 observer build** did not compile (the observer had no LR2021 radio case).
- **Dev container.** A VS Code dev container with the same SDK, toolchains and blobs as CI, so the
  firmware can be built without setting anything up on your own machine. Thanks to **mepholic**.
- **Platform: Zephyr moved from `v4.4-branch` to `main`** (`3e8f38faf93`). See the section above.
- **A belated thank-you** to **JCBird1012**, whose work made ESP32-S3 companions talk to the app over
  USB. It shipped in 1.17.4 without a mention.
