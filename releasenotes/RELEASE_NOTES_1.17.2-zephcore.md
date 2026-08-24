# ZephCore 1.17.2-zephcore

Fixes across most of the radio types, two new boards, and colour screens on the Wireless Trackers.

> [!NOTE]
> Upgrading from 1.17.1 is straightforward — your contacts, settings and phone pairing all survive. Note
> that the **factory default radio settings changed to SF7 / CR 4/5**; this affects fresh flashes and
> factory resets only, never a node that already has settings.

> [!NOTE]
> **About testing.** ZephCore supports many more boards than anyone working on it actually owns. Most
> changes are tried on a handful of real nodes and only compiled for the rest, so a fix proven on one
> radio is usually taken on trust for every board sharing that radio. Tune your expectations accordingly:
> the further your board is from the common ones, the more likely you are the first person running a
> given change on it. If something looks wrong, please say so — that is genuinely how most of these get
> found.

---

## New nodes start on SF7 / CR 4/5

A node with no saved settings — freshly flashed, or after a factory reset — now comes up on
**SF7, coding rate 4/5**. It was SF8 / 4/8. Frequency (869.618 MHz), bandwidth (62.5 kHz) and
transmit power are unchanged.

Hungary moved its mesh to these settings, and ZephCore is aimed mainly at Hungarian users, so this is
now what a node uses out of the box. On SF7 / 4/5 a packet spends roughly a third of the time on air
that it did on SF8 / 4/8, which leaves the channel free for far more traffic. The trade is a little
less range on the weakest links.

> [!IMPORTANT]
> **Nodes that already have settings are not touched.** Upgrading to 1.17.2 leaves your frequency,
> spreading factor, coding rate and power exactly as you set them. Nothing is rewritten and nothing
> needs checking afterwards.

> [!WARNING]
> **A factory reset now leaves the node on SF7 / 4/5.** If your mesh still runs SF8 / 4/8, a node that
> has been reset will not hear it and will not be heard — radios only talk to other radios using the
> same settings. Put it back with `set radio 869.618,62.5,8,8` after a reset, or move the whole mesh to
> SF7 / 4/5.

To change a node already in service, use `set radio <freq>,<bw>,<sf>,<cr>` on a repeater (the four
values are comma-separated, and it needs a reboot to apply), or the radio settings screen in the phone
app for a companion.

## New board: LilyGo T3S3

The **LilyGo T3S3** is now supported — ESP32-S3 with an OLED screen, a button and a battery gauge.

> [!IMPORTANT]
> **The SX1262 version only.** LilyGo sells this board with four different radios (SX1262, SX1276, SX1280,
> LR1121) under one name. Flashed onto any of the others it boots and looks fine but never transmits or
> receives. Check which radio your board has first.

## New board: MinewSemi ME25LS02

The **ME25LS02** is now supported — a MinewSemi module pairing an nRF54L15 with an SX1262, aimed at
people building low-cost repeaters. Both companion and repeater builds are provided, and the module's
LLCC68 variant uses the same firmware. Requested by **xpiREC**
([@xpiREC](https://github.com/xpiREC)), who also published the development board's documentation —
[#68](https://github.com/liquidraver/ZephCore/issues/68).

> [!IMPORTANT]
> **This board can only be flashed with an SWD probe** — a J-Link or similar. The nRF54L15 has no USB
> hardware at all, so there is no drag-and-drop UF2 file and no update over a cable. The USB-C socket on
> the development board is a serial adapter for the console, not a programming port. The firmware is
> published as a `.hex` file, and the Mesh America configurator lists the board as a download rather than
> offering to flash it.

The development board's RGB indicator and its user button both work, and the console appears on the
USB-C serial port at 115200 baud.

> [!NOTE]
> **The module's own datasheet and schematic disagree with each other.** They give different pins for
> three of the radio's control lines, and the datasheet lists one pin twice. ZephCore follows the
> schematic. Worth knowing if you are wiring this module into something of your own, or comparing
> against other firmware for it.

> [!NOTE]
> **The node keeps time less precisely than most boards.** The module has no 32.768 kHz crystal of its
> own, so ZephCore runs the chip's internal oscillator instead — accurate to about ±250 ppm rather than
> the ±50 ppm a crystal gives. Everything works normally; the clock simply drifts a little faster.

## The Wireless Trackers get their colour screen

The **Wireless Tracker** and **Wireless Tracker V2** have had a colour screen all along — the same panel
as the Heltec T096 — but ZephCore was drawing on it in black and white. Both now show what the T096
shows: coloured status badges, coloured text and the activity graph. The pages and what they say are
unchanged, only how they are drawn.

The Wireless Tracker V2's button also picks up the gestures the other screen boards already had. One tap
and two taps still move between pages; **three taps** toggles the LED heartbeat, **four** turns GPS on
and off, and **five** sends an advert.

## Commands to your own node get their tick straight away

When you send a command to your node's own chat contact (the one named `v` plus your node name), the app
now marks it delivered immediately.

Before, the tick took a few seconds to appear, and while the app was waiting it quietly sent the command
a second time. Mostly harmless — but `reboot` ran twice, so your node rebooted again right after coming
back up.

## XIAO nRF52840 and Ikoka Nano 30 dBm charge twice as fast

These boards were charging at 50 mA. They now charge at **100 mA**, so a battery fills in roughly half
the time.

A battery-measurement pin was also putting close to the maximum voltage the chip is rated for onto one of
its inputs whenever it took a reading. It no longer does. Battery readings are unchanged.

> [!WARNING]
> **Only if your XIAO sits on a carrier board with its own charger** (a TP4056 module, for example):
> remove the `hichg` GPIO hog from the board DTS before flashing. Two chargers must never charge the same
> battery at once.

## T1000-E reports temperature and light again

The T1000-E has a real temperature sensor and a light sensor built in, and ZephCore was ignoring both.
The app only showed battery voltage and the chip's own internal temperature — which reads about **3 °C
too warm**, because it measures the chip, not the room. Both sensors are now read and sent along with
everything else.

The readings match Seeed's original firmware, so your node reports the same numbers after reflashing as
it did before.

> [!NOTE]
> Light is reported as a **0–100 brightness level, not lux**. It is the sensor's output voltage rescaled
> onto 0–100 — nothing converts it into a real light unit, and Seeed's original firmware does the same.
> Normal indoor lighting sits in the bottom few percent and only direct light moves it much — that is the
> sensor's real range, not a stuck reading.

The T1000-E also boots about **3.5 seconds faster** and has around **7 KB more free space**. It had been
looking for plug-in sensors that physically cannot be attached to it — it is a sealed tracker with no
connector — and each one took half a second to give up on.

Reported by **Kimotu** ([#71](https://github.com/liquidraver/ZephCore/issues/71)). T1000-E only; no other
board loses its sensor detection.

## Repeaters accept guest logins without a password again

1.17.1 made an empty guest password mean "guests not allowed". That is correct for room servers, but
wrong for repeaters — a repeater with no guest password is supposed to be open to guests, the same way
Arduino MeshCore does it. Those logins were being refused. They work again.

Guests on a repeater get login, status and telemetry. They cannot run commands or see the access list.
Set `guest.password` if you want one required.

**Room servers are deliberately left as they are:** no guest password still means no guest access, so a
room is never accidentally left wide open. Use `allow.read.only on` if you want an open room.

`allow.read.only` no longer shows up in the repeater's command list. Only room servers ever used it, so
on a repeater it was a setting that did nothing. Your saved configuration is untouched.

## Empty channel slots no longer pick up stray traffic

A channel slot you never set up used to accept group messages sent with no key at all, and file them under
that empty slot. Unused slots are now skipped, so those messages are simply ignored.

## A node that cannot get a word in now tells you

If your node keeps finding the channel busy and can never transmit, it now says so after four seconds and
flags it as an error. Before, a node that could hear everyone but never got heard back looked completely
idle, with nothing in the log to explain it. (in debug logs)

All roles, all boards.

## Duty-cycled nodes keep the radio off a bit longer

Nodes using receive duty cycling now spend more time with the radio asleep — on a typical SF7 setup,
radio-off time goes from about **31 % to about 35 %**. All radio types benefit.

> [!NOTE]
> If a node suddenly starts losing packets for no obvious reason — random losses while the signal looks
> perfectly fine — set `CONFIG_ZEPHCORE_LORA_DC_MARGIN_PCT` back to 15 before looking at anything else.
> This kind of loss has nothing to do with signal strength, so signal readings will not show it.

## A radio that has gone deaf now recovers on its own

An SX126x receiver can get stuck at the wrong gain and quietly stop hearing anything. Nothing looks wrong —
the node runs, the screen updates, it simply never receives. Until now the only cure was a reboot.

The radio now resets its own receiver, but only on real evidence: a long silence **and** a noise-floor
reading that has stopped moving. Silence alone is not a fault — a quiet channel is normal — and resetting
on it cost packets, because the reset lands exactly when traffic resumes. A busy node essentially never
does this; a genuinely deaf one recovers without a reboot.

This replaces the old `agc.reset.interval` setting, which ran on a timer whether it was needed or not and
shipped switched off. It is automatic now and needs no configuration.

**SX126x boards only.** This is a fault Semtech describe for that family; the LR1110 and LR2021 do not
have it, and running the reset on them was losing packets rather than saving any. Those radios instead
recalibrate their front end after a 5 °C temperature swing — the thing their datasheets *do* ask for —
using the board's own temperature sensor rather than the radio's, so it costs the radio nothing.

## Your node can no longer advertise an identity it cannot prove

Your identity is a private key and a matching public key. If those two ever stopped matching — a damaged
file, a bad write — your node would keep announcing a public key it could not actually use, forever, and
nothing would notice. In that state nobody can verify your adverts and direct messages to you cannot be
opened.

One node in the field turned up exactly like this after moving over from Arduino MeshCore. The public key
can always be recalculated from the private one, so the node now recalculates it at every boot instead of
trusting the stored copy. A mismatch shows up in the log instead of passing silently.

Identity files are now stored in the same format Arduino MeshCore uses, so a file can be moved between
the two projects. Every older format is still readable — nothing needs converting and no existing file is
rewritten. If a file is too damaged to make sense of, the node keeps it as `_main.id.bad` and generates a
fresh identity rather than throwing the old one away.

Your saved settings now get a similar check. Every value read back from storage is tested against the
range it is allowed to be in, and anything nonsensical falls back to its default instead of being used. A
few of those are the difference between a working node and one that looks dead: a corrupted shutdown
voltage would switch the node off seconds after boot, and a corrupted BLE pairing code would lock the
phone app out with no way back in.

Found and diagnosed by **Marcel Verdult** ([@marcelverdult](https://github.com/marcelverdult)) —
[#70](https://github.com/liquidraver/ZephCore/pull/70).

> [!WARNING]
> **Only if you go back to 1.17.1 or older after a factory reset or fresh install.** An identity created
> on 1.17.2 uses the new format, and older firmware will misread it — the node comes up with a broken
> identity. Nothing is lost: older firmware misreads the file but never rewrites it, so flashing 1.17.2
> again brings the identity back exactly as it was. Nodes simply upgraded from 1.17.1 keep their existing
> file and are not affected at all.

## MeshTracker X1: less time deaf when the channel is busy

Every time the X1 held off transmitting because the channel was busy, it rebuilt its receiver twice
instead of once. Now it does it once, halving the time it spends unable to hear anything.

LR2021 boards only.

## New setting: switch the antenna amplifier's receive gain off

Some boards carry an extra amplifier chip between the radio and the antenna. It boosts what the node
transmits, and on the way in it boosts what the node hears. It has always been switched on for both, and
it stays that way — **nothing changes unless you change it.**

The new `set radio.fem.rxgain 0` switches off only the receive side, to save the current that part of the
amplifier draws. Transmitting is untouched: every packet the node sends still goes out through the
amplifier at full strength. `set radio.fem.rxgain 1` puts it back, and `get radio.fem.rxgain` shows where
it stands. The node keeps the setting across reboots.

Supported on the **Heltec T096**, **Wireless Tracker V2**, **WiFi LoRa 32 V4** and **WiFi LoRa 32 V4.3**.
Everything else replies `Error: unsupported` — either the board has no such amplifier, or its amplifier is
switched on by a line the radio driver cannot reach. The **RAK3401 1 W** is in that second group.

Proposed by **bisbille** ([@bisbille](https://github.com/bisbille)) —
[#74](https://github.com/liquidraver/ZephCore/pull/74).

## T1000-E and other LR1110 nodes: receive duty cycling actually works now

If you run a **T1000-E** with `rxduty` switched on, it was missing most of what it should have heard —
measured on a bench against an identical node with a different radio, **10 packets against 94** over the
same 85 minutes. The node looked perfectly healthy the whole time.

The radio's power-saving receive loop ends whenever the host talks to the chip at the wrong moment, and
nothing noticed or restarted it, so the node simply went deaf until something else happened to wake it.
Older releases hid this by restarting the receiver on a timer; when that timer was removed the problem
became visible. The driver now takes charge of the loop directly — it stops it deliberately when it needs
the radio, and starts it again afterwards. Same bench test after the fix: **10 against 10**.

Two things fixed themselves along the way. The node can now measure its own noise floor — on a duty-cycled
LR1110 it never could, so the adaptive channel-busy detection had nothing to work with and never
calibrated. And the check that stops a node transmitting over a packet it is already receiving was
answering "not receiving" almost always, which quietly disabled it.

**LR1110 boards** (T1000-E, ThinkNode M9, Wio variants and others). If you switched `rxduty` off
because the node seemed unreliable, it is worth switching back on.

## Contacts survive a power cut, and the flash lasts longer

On boards with no external flash chip — the T1000-E among them — saving your contact list **deleted the
file first and then wrote it again**. Losing power anywhere in that write left the node with no contact
list at all, rather than a damaged one. It now writes over the file in place, so the file always exists.

Separately, the contact list was being rewritten in full every time any known contact was simply heard
again. On a busy mesh that was a full rewrite every few minutes, for information that had not meaningfully
changed. Those refreshes now wait and get written together — measured on a bench as **six writes down to
one** for the same traffic. Anything that actually matters (a new contact, a message, a route change) is
still saved within seconds.

Boards with an external flash chip already wrote contacts safely and continue to; they get the reduced
write rate too.

## A phone that connects but never pairs no longer blocks everyone else

A node has room for one Bluetooth connection. If something connected and then never completed pairing —
common with iOS, and easy to do by accident with a scanner app — the node held that slot open forever,
stopped advertising, and became invisible to everyone including you. A power cycle was the only way back.

Connections that have not paired within 15 seconds are now dropped and advertising resumes. Nothing a
real client does is affected: every part of the node's Bluetooth service requires pairing anyway, so an
unpaired connection cannot do anything with the slot it is holding.

## Nodes that report to a map now say whether they relay

A node with a WiFi uplink, and the listen-only observer role, now includes in what it reports whether
it forwards other people's packets. Map sites use this to tell a repeater apart from a node that only
listens, and to work out who actually carried a packet — without it they had to guess, and showed a
dash where the answer should be.

An observer always reports that it does not relay, because it never does. A repeater reports whatever
`repeat` is set to, so turning forwarding off is reflected on the map.

Nodes bridged to a map through a Raspberry Pi instead of their own WiFi are unaffected by this — that
comes from the bridge software on the Pi, which needs its own update.

## Command replies match Arduino MeshCore again

Apps read a node's replies literally, and a few of ours were worded differently enough to be misread.
`clock` answered with `Clock: ` in front of the time, which made some apps flag the node's clock as
wrong when it was perfectly correct. The prefix is gone — the reply is now just the time, exactly as
Arduino MeshCore sends it.

Four more replies were brought back into line:

- `get radio.rxgain` and `get radio.fem.rxgain` answer **`on` or `off`**, not `1` or `0`.
- `get radio` reports a 250 kHz bandwidth as **`250`**, not `250.0`.
- `get extra.sf` lists the spreading factors on their own, and says `No extra SF configured` when there
  are none.
- `get cad` answers **`on`**. ZephCore always listens before transmitting and has no switch for it, so
  that is simply the truth. The detailed statistics that used to appear here moved to **`get cad.stats`**,
  unchanged.

Only the wording changed — no setting behaves differently, and nothing needs adjusting after the update.

---

## Also in this release

Housekeeping, listed for completeness — nothing here changes how a node behaves day to day.

- **Three boards that were supported but never published now ship firmware with every release.** All
  three have been buildable for a while but were missing from the published build, so they had to be
  compiled by hand. Companion and repeater images are now built and listed alongside every other
  board:
  - **XIAO nRF54L15** — like the ME25LS02, a `.hex` flashed over SWD, though the XIAO's own USB cable
    is enough since it has a debug bridge built into the board.
  - **XIAO MG24** — also SWD-only, and the first Silicon Labs board to get published firmware.
  - **LoRa-E5 mini** — SWD-only. No Bluetooth on this chip, so the companion talks to the app over
    the USB serial port instead.
- **The Zephyr operating system underneath was updated** to a newer snapshot, and our radio patches were
  reorganised on top of it: five separate SX126x patches are now one. Same behaviour, fewer things to go
  wrong the next time Zephyr moves.
- **Sending a raw packet down a known route now works at every path-hash setting.** Only diagnostic tools
  use this, never normal messaging, and it rejected every route on a node set to a longer path hash.
- **`get`/`set agc.reset.interval` now reply `Removed - Automatic AGC reset is on`** instead of pointing at
  `rxduty`. The setting has been gone for a while; only the wording changed.
- **LR1110 and LR2021 radios no longer mis-read a reply from the chip as packet data.** A timing race
  could hand the driver the chip's status registers instead of the answer it asked for, and the resulting
  nonsense packet length was thrown away silently. Ported from Arduino MeshCore
  ([PR #3261](https://github.com/meshcore-dev/MeshCore/pull/3261)).
- **The repeater command reference was corrected in several places** — `set radio` and `tempradio` take
  comma-separated values, not spaces; `set prv.key` takes the 128-character key; `allow.read.only` and
  `buzzer` are room-server only. The commands themselves did not change, the documentation was simply
  wrong about them.
