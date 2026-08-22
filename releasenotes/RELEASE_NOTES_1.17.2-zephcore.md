# ZephCore 1.17.2-zephcore

A small release. Most of it is fixes, and a few of them only matter on specific boards.

> [!NOTE]
> **Worth updating if you have:** a **XIAO nRF52840** or **Ikoka Nano 30 dBm** (charges twice as fast now),
> a **T1000-E** (temperature and light sensors work again), or **any repeater** (1.17.1 broke guest logins
> without a password). Everyone else can wait for the next release.
>
> Upgrading from 1.17.1 is straightforward — your contacts, settings and phone pairing all survive.

---

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
> Light is reported as **0–100, not in lux**. That is what the sensor and the original firmware produce.
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

---

## Not fully tested yet

- **The radio timing changes have not been measured on air.** Both are worked out on paper and build
  clean, but neither has been checked against real traffic.
- **The repeater guest-login fix has not been tried on hardware**, only verified to build on both roles.
- **The T1000-E sensor readings are confirmed on a real device**; the faster boot time is measured from a
  before-log only, with no after-log yet.
- **The identity repair paths have not been tried on hardware.** Every case is covered by tests on a PC,
  but no real node has been broken and recovered. Normal healthy nodes are unaffected.
- **Why that one node's identity broke is still unknown.** This release makes sure the same thing can no
  longer go unnoticed, but the original cause has not been found.
- **The reported X1 trouble logging in to repeaters is not fixed here** and is still being looked into.
