# ZephCore 1.17.3-zephcore

One fix and two new settings. The antenna amplifier setting added in 1.17.2 switched the amplifier off
completely instead of just its receive boost, leaving those nodes barely able to hear anything. And for
people who put their node in a case it did not come in, the screen can now be turned upside down and the
joystick flipped to match.

> [!NOTE]
> Upgrading from 1.17.2 is straightforward — your contacts, settings and phone pairing all survive.
> **If you never touched `radio.fem.rxgain`, nothing about your node changes.** The default is on, and
> on behaves exactly as it did. Both new settings default to off, so a node you do not touch behaves
> exactly as it did too.

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

## The screen can be turned upside down

DIY builds and upgrade kits do not always hold the board the way the original case did. The
**Meshnology N37E**, for instance, takes a Wio Tracker L1 and puts it in a different enclosure — one
that mounts the screen the other way up. Until now the only answer was to build your own firmware with
the panel flipped, which meant giving up the published builds everyone else uses.

`set display.rotate on` turns the picture 180 degrees. It survives reboots, and the same firmware
everybody else runs is now the firmware you run too.

On the boards with a joystick there is a menu entry as well — **System → Display → Rotate 180** — so it
can be set on the device without a cable.

The rotation costs nothing to keep on. It is not done by redrawing the screen the other way round; the
firmware asks the display chip itself to scan its rows and columns in reverse, which is two bytes sent
once. Every frame after that is drawn exactly as before.

> [!IMPORTANT]
> **This works on OLED screens only** — the SSD1306 and SH1106 panels, which covers the **RAK4631**,
> **GAT562 30S**, **Heltec WiFi LoRa 32 V4** and **V4.3**, **LilyGo T3S3**, **Station G2** and
> **Wio Tracker L1**.
>
> Boards with a colour screen or e-paper answer `Error: this panel cannot rotate`, and the setting is
> not saved — so a stored value can never claim something the screen is not doing. Those panels can be
> rotated in principle, but it would mean turning every frame around in software rather than asking the
> chip to do it, and that is a different piece of work.

> [!NOTE]
> **The LilyGo T-Impulse Plus is the one OLED board left out.** Its small screen only occupies part of
> the display chip's memory, and the trick that flips the picture flips the chip's whole range — which
> would have slid the image off the visible area rather than turning it over. It reports the setting as
> unsupported, like the colour and e-paper boards.

> [!NOTE]
> On the Wio Tracker L1 and Station G2 the flip relies on their screen being wired the way these
> modules normally are. It was reasoned from the board description rather than measured on a bench, so
> if a rotated screen comes out shifted a few columns sideways rather than cleanly turned over, please
> say so.

## The joystick can be flipped to match

Turning the screen over does not turn the buttons over. A case that mounts the board upside down puts
the joystick upside down as well, so pushing up walks the menu down and left goes right.

`set input.rotate on` swaps both axes back. On the joystick boards there is a menu entry next to the
rotation one — **System → Display → Flip input**.

It is deliberately a **separate** setting rather than part of the rotation. A case can put the screen
somewhere new without moving the stick, and boards whose screen cannot rotate can still end up with the
controls the wrong way round — so tying the two together would have made both less useful. Set whichever
you actually need.

This one works on every board with directional controls, including the ones whose screen cannot rotate.
Only the four directions are affected: buttons, tap gestures and long presses are untouched, and on the
single-button boards the flip swaps which way the pages step.

Both settings are readable with `get display.rotate` and `get input.rotate`. `get display.rotate`
reports what the panel is actually doing rather than what is stored, so a refused rotation shows up as
refused.

Suggested by **celsoazevedo** ([@celsoazevedo](https://github.com/celsoazevedo)) in
[issue #76](https://github.com/liquidraver/ZephCore/issues/76).

---

## Also in this release

Nothing here changes how a node behaves.

- **A build fix.** After resetting the Zephyr sources by hand, a rebuild could quietly skip re-applying
  ZephCore's patches and produce firmware without them. It now checks the sources instead of trusting a
  marker file. This only ever affected people building from source.
