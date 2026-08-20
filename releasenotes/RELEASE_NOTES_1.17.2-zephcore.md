# ZephCore 1.17.2-zephcore

A **minor release.** Three small changes, two of which only affect specific hardware. If you are running
1.17.1 on anything other than a XIAO nRF52840 or an Ikoka Nano 30 dBm, there is little reason to flash
this.

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

---

## Known limitations

- **The two radio changes are not on-air validated** — both are build-verified and reasoned from the
  datasheet, not measured on a live mesh.
- **The reported X1 difficulty logging in to repeaters is not fixed here** and remains under
  investigation. The listen-before-talk reporting above will make that failure visible in the log if it
  is the cause, which is one of several open hypotheses.
