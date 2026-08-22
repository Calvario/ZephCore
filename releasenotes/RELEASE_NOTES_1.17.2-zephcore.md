# ZephCore 1.17.2-zephcore

A small release. Most of it is fixes, and a few of them only matter on specific boards.

> [!NOTE]
> **Worth updating if you have:** a **XIAO nRF52840** or **Ikoka Nano 30 dBm** (charges twice as fast now),
> a **T1000-E** (temperature and light sensors work again), a **LilyGo T3S3** (supported for the first
> time), or **any repeater** (1.17.1 broke guest logins without a password). Everyone else can wait for
> the next release.
>
> Upgrading from 1.17.1 is straightforward — your contacts, settings and phone pairing all survive. Note
> that the **factory default radio settings changed to SF7 / CR 4/5**; this affects fresh flashes and
> factory resets only, never a node that already has settings.

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
idle, with nothing in the log to explain it.

All roles, all boards.

## Duty-cycled nodes keep the radio off a bit longer

Nodes using receive duty cycling now spend more time with the radio asleep — on a typical SF7 setup,
radio-off time goes from about **31 % to about 35 %**. All radio types benefit.

> [!NOTE]
> If a node suddenly starts losing packets for no obvious reason — random losses while the signal looks
> perfectly fine — set `CONFIG_ZEPHCORE_LORA_DC_MARGIN_PCT` back to 15 before looking at anything else.
> This kind of loss has nothing to do with signal strength, so signal readings will not show it.

## A radio that has gone deaf now recovers on its own

A LoRa receiver can get stuck at the wrong gain and quietly stop hearing anything. Nothing looks wrong —
the node runs, the screen updates, it simply never receives. Until now the only cure was a reboot.

After a full minute with nothing heard at all, the radio now resets its receiver by itself. A busy node
essentially never does this, because every packet it receives is proof the receiver works; a silent node
loses nothing by trying, because there was nothing to miss. Radios that can measure their own temperature
also recalibrate after a 5 °C swing, so a node that boots on a warm afternoon still works through a cold
night.

This replaces the old `agc.reset.interval` setting, which ran on a timer whether it was needed or not and
shipped switched off. It is automatic now and needs no configuration.

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

## An LR1110 too old to reach the mesh now says so

An LR1110 running firmware older than 0x0303 cannot be moved off the public LoRa channel. It transmits
and receives perfectly well, but nobody on your mesh can see it. That now appears in the log instead of
looking like a broken radio.

## New setting: switch the antenna amplifier's receive gain off

Some boards carry an extra amplifier chip between the radio and the antenna. It boosts what the node
transmits, and on the way in it boosts what the node hears. It has always been switched on for both, and
it stays that way — **nothing changes unless you change it.**

The new `set radio.fem.rxgain 0` switches off only the receive side, to save the current that part of the
amplifier draws. Transmitting is untouched: every packet the node sends still goes out through the
amplifier at full strength. `set radio.fem.rxgain 1` puts it back, and `get radio.fem.rxgain` shows where
it stands. The node keeps the setting across reboots.

> [!WARNING]
> **This costs range, and a lot of it.** With the receive side off the node goes substantially deafer —
> on a Wireless Tracker V2 the measured noise floor moves by about 23 dB between the two settings. Distant
> and weak neighbours simply stop being heard, while the node's own transmissions carry exactly as far as
> before, so from the outside it still looks perfectly healthy. Only worth doing on a battery-powered node
> where you already know every neighbour is close and strong.

Supported on the **Heltec T096**, **Wireless Tracker V2**, **WiFi LoRa 32 V4** and **WiFi LoRa 32 V4.3**.
Everything else replies `Error: unsupported` — either the board has no such amplifier, or its amplifier is
switched on by a line the radio driver cannot reach. The **RAK3401 1 W** is in that second group.

Proposed by **bisbille** ([@bisbille](https://github.com/bisbille)) —
[#74](https://github.com/liquidraver/ZephCore/pull/74).

---

## Also in this release

Housekeeping, listed for completeness — nothing here changes how a node behaves day to day.

- **The Zephyr operating system underneath was updated** to a newer snapshot, and our radio patches were
  reorganised on top of it: five separate SX126x patches are now one. Same behaviour, fewer things to go
  wrong the next time Zephyr moves.
- **Sending a raw packet down a known route now works at every path-hash setting.** Only diagnostic tools
  use this, never normal messaging, and it rejected every route on a node set to a longer path hash.
- **`get`/`set agc.reset.interval` now reply `Removed - Automatic AGC reset is on`** instead of pointing at
  `rxduty`. The setting has been gone for a while; only the wording changed.
- **The repeater command reference was corrected in several places** — `set radio` and `tempradio` take
  comma-separated values, not spaces; `set prv.key` takes the 128-character key; `allow.read.only` and
  `buzzer` are room-server only. The commands themselves did not change, the documentation was simply
  wrong about them.

---

## Not fully tested yet

- **The T1000-E sensor readings are confirmed on a real device**; the faster boot time is measured from a
  before-log only, with no after-log yet.
- **The identity repair paths have not been tried on hardware.** Every case is covered by tests on a PC,
  but no real node has been broken and recovered. Normal healthy nodes are unaffected.
- **Why that one node's identity broke is still unknown.** This release makes sure the same thing can no
  longer go unnoticed, but the original cause has not been found.
- **The antenna amplifier receive setting is compile-checked on every affected board, but has not been
  measured on hardware in this form.** The 23 dB figure above comes from the original proposal, which
  reached the same pin by a different route. Leaving the setting alone keeps every node behaving exactly
  as it did before, so a node nobody deliberately switches over is not affected at all.
