# ZephCore 1.17.3-zephcore

One fix. The antenna amplifier setting added in 1.17.2 switched the amplifier off completely instead
of just its receive boost, leaving those nodes barely able to hear anything.

> [!NOTE]
> Upgrading from 1.17.2 is straightforward — your contacts, settings and phone pairing all survive.
> **If you never touched `radio.fem.rxgain`, nothing about your node changes.** The default is on, and
> on behaves exactly as it did.

---

## `radio.fem.rxgain 0` no longer makes a node deaf

Some boards carry an extra amplifier chip between the radio and the antenna. 1.17.2 added
`set radio.fem.rxgain 0` to switch off its receive boost and save the current that part draws, while
leaving transmit untouched.

It switched off the entire amplifier instead. With the chip powered down, nothing reaches the receiver
— not the boost, and not the signal either. Measured on a Heltec V4.3 that was about **69 dB** of loss,
where the setting promised around 16 dB. One node could no longer hear its own repeater from 50 metres
away.

The setting now does what it says. The signal goes **around** the amplifier's receive boost rather than
through it, costing about **17 dB**, and the chip stays powered — so transmit, and everything else, are
unaffected. If you deliberately switched it off to save power, it now costs you what it was always
supposed to cost.

Available on the **Heltec T096**, **Wireless Tracker V2** and **WiFi LoRa 32 V4.3**.

> [!IMPORTANT]
> **The WiFi LoRa 32 V4 no longer has this setting.** Its amplifier is a different chip with no separate
> receive-side control, so there was never a working version of this on the V4 — it only ever got the
> broken behaviour. It now replies `Error: unsupported`. If you had switched it off on a V4, that node
> gets its hearing back, and simply stops saving the current it was trying to save.

> [!NOTE]
> The 17 dB figure was measured on a V4.3. The T096 and Wireless Tracker V2 use the same amplifier and
> are wired the same way, but were not measured — if the number looks different on yours, please say so.

---

## Also in this release

Nothing here changes how a node behaves.

- **A build fix.** After resetting the Zephyr sources by hand, a rebuild could quietly skip re-applying
  ZephCore's patches and produce firmware without them. It now checks the sources instead of trusting a
  marker file. This only ever affected people building from source.
