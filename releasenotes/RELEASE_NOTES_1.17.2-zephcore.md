# ZephCore 1.17.2-zephcore

A **minor release.** Four changes, two of which only affect specific hardware, plus a hardening pass on
how the node's identity is stored.

> [!NOTE]
> **Most people can skip this.** The one group that should not is **XIAO nRF52840 and Ikoka Nano 30 dBm**
> owners — those boards get twice the charge current and a battery-sense pin that no longer sits at the
> nRF52840's absolute maximum. Everyone else: safe to skip and pick these up with the next real release.
>
> Straight upgrade from 1.17.1 either way — bonds and data survive, no re-pairing.

---

## XIAO nRF52840 / Ikoka Nano 30 dBm: 100 mA charging, and a pin fix

The BQ25100 on the XIAO module selects its charge current from the HICHG pin, and left floating — the
reset default — it charges at **50 mA**. That pin is now driven, so these boards charge at **100 mA**,
roughly halving the time to fill a pack. Matches Arduino MeshCore's `variants/xiao_nrf52` and
`variants/ikoka_nano_nrf`.

The battery ADC divider enable (P0.14) is now held low permanently instead of toggled per reading.
Driving it high puts about **3.6 V on the AIN7 node with a 4.2 V cell** — the nRF52840 GPIO absolute
maximum, with no margin, and over it on a warm pack. Holding it low parks the node near 1.4 V and costs
about 2.8 µA, which is nothing beside BLE and LoRa receive.

> [!WARNING]
> **If your XIAO sits on a carrier board with its own charger** — a TP4056 module, for example — delete
> the `hichg` GPIO hog from the board DTS before flashing. Two chargers must not drive the same pack.

## Less receiver downtime when the channel is busy — LR2021 only

On the MeshTracker X1, every transmit that listen-before-talk refused rebuilt the receiver **twice**
before retrying, because the driver restored RX and then the layer above it did too. One of those is now
a no-op, halving the time the node spends deaf per refused attempt.

No effect on any other board.

## A muted node now says so

If listen-before-talk keeps refusing to transmit, that is reported after four seconds of continuous
refusal and flagged in the node's error status. Previously it was logged below debug level with no
counter, so a node that could hear the mesh but never got a word in looked completely idle.

All roles, all boards.

## A node can no longer advertise a key it cannot sign for

Your identity is a private key plus the public key derived from it. Those were stored side by side and
read back on trust, so if the two halves ever stopped matching — a stale file, a foreign write — the node
would advertise a public key it had no private key for and keep doing so through every reboot. Nothing
ever re-checked. In that state adverts verify nowhere, direct messages to you cannot be decrypted, and
the companion app is handed an exported key that contradicts what the node says about itself.

One node in the field turned up like this after moving from Arduino MeshCore, which is how it was found.
The public half is redundant — it can always be recomputed from the private key — so it is now treated as
a checksum rather than as data: the node derives its public key on every boot and never reads the stored
copy. If the two disagree, that is now visible in the log instead of silent.

Storage also moves to Arduino MeshCore's byte order (`pub || prv`), so all three roles finally agree on
one format and an identity file can be moved between the two projects. Every layout either project has
ever written is still readable, including the 64-byte prv-only files older repeaters wrote — nothing
needs migrating and no existing file is rewritten.

If a file cannot be made sense of at all, the node tries each half as a private key and adopts one only
if exactly one is genuine. Failing that it keeps the bytes as `_main.id.bad` and generates a fresh
identity, so a broken key is never silently thrown away.

Found and diagnosed by **Marcel Verdult** ([@marcelverdult](https://github.com/marcelverdult)) —
[#70](https://github.com/liquidraver/ZephCore/pull/70). His writeup is what made the byte-order
difference findable.

All roles, all boards.

> [!WARNING]
> **Downgrading below 1.17.2 after a factory reset or a fresh install.** Nodes carried over from 1.17.1
> keep their existing identity file untouched and are unaffected. But an identity *generated* on 1.17.2
> (or re-imported into it) uses the new byte order, and 1.17.1 and earlier will misread it — the node
> comes up with a broken identity. Nothing is lost: older firmware misreads the file but does not rewrite
> it, so flashing 1.17.2 again restores the identity exactly as it was.

---

## Known limitations

- **The two radio changes are not on-air validated** — both are build-verified and reasoned from the
  datasheet, not measured on a live mesh.
- **The identity repair and regenerate paths are not exercised on hardware.** Every layout and failure
  case is covered by host tests against the same crypto library the firmware uses, and all three roles
  build clean, but no physical node has been put into a broken state and recovered. The normal path — a
  healthy node loading a healthy identity — is unchanged apart from one extra key derivation at boot.
- **How that one node's key halves stopped matching is still unexplained.** The byte-order difference
  reproduces the symptom exactly, but it should not have been reachable on that hardware, so something
  else may have put it there. What this release fixes is that the state can no longer go unnoticed.
- **The reported X1 difficulty logging in to repeaters is not fixed here** and remains under
  investigation. The listen-before-talk reporting above will make that failure visible in the log if it
  is the cause, which is one of several open hypotheses.
