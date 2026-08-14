# ZephCore 1.17.1-zephcore

A **hardware release**. A new tracker board lands — the Seeed SenseCAP MeshTracker X1 — and with it the
LR2021 radio becomes a real, on-air-validated option instead of bring-up code. Plus GPS diagnostics, an
LED master switch, and a batch of receive- and transmit-path hardening.

> [!NOTE]
> **1.17.0 was never published**, so this release carries everything that was written up for it. If you
> are coming from 1.16.8, everything below is new to you.

---

> [!IMPORTANT]
> ## Before you upgrade
>
> **From v1.16.5 – v1.16.8** — just flash it. Bonds and data survive, no re-pairing.
>
> **From v1.16.2 – v1.16.4 on an ESP32-S3 / ESP32-C board** — you still owe a one-time **USB/serial
> reflash of `-merged.bin`**. The app moved to flash offset `0x10000` in v1.16.5, and a board on the old
> layout cannot cross that gap over WiFi-OTA or the browser flasher. Affected: *Heltec V3 / V4 / V4.3,
> Station G2, Wireless Tracker / V2, XIAO ESP32-S3 / C3 / C6, LilyGo T-Lora C6, ThinkNode M9.* Your
> identity, contacts, channels, prefs and bonds are preserved. Everything else (nRF52, classic ESP32,
> STM32WL) upgrades normally.
>
> **From v1.16.1 or older** — flash it; BLE bonds are cleared on first boot, so re-pair your phone once.
> Identity, contacts, channels and prefs are kept.
>
> **Coming from Arduino MeshCore** — flash it; storage auto-formats and you get a new identity.
>
> **ThinkNode M6 owners** — this release stops using the external flash chip, because the board doesn't
> actually have one. Contacts, channels and prefs move to internal flash on first boot.
>
> **Repeater / room-server telemetry no longer shares GPS position with non-admins.** If you were
> relying on that, set the location in prefs and advertise it instead.
>
> **ESP32 light sleep** stays on by default for Heltec repeaters. It has now been confirmed working on a
> Heltec V4 — but only that one board so far. Side effect: the USB CLI stops answering ~10 minutes after
> boot; reconnect the terminal to get another window. Rebuild with `-DCONFIG_PM=n` to remove it.

---

## Slow presets no longer lose long packets

The biggest fix in this release, and it is invisible until you run a narrow bandwidth. Transmission was
bounded by two fixed deadlines that are shorter than the airtime of many perfectly legal presets:

- The **chip's own transmit timer** was pinned at 10 s. The SX126x datasheet is explicit that when this
  timer fires the transmission is *stopped* — so on slow presets it was not a safeguard, it was a
  truncation. A full-size packet is 17.7 s at SF10/BW31.25 and 28.6 s at SF12/BW62.5; those presets
  were cutting every packet from ~136 bytes and ~76 bytes up, mid-air.
- The **host's wait for "transmit finished"** was a fixed 5 s, after which the radio was yanked back
  into receive — killing a transmission that was going perfectly well.

Both now scale from the driver's own airtime calculation, floored at the old values so nothing that
works today gets a tighter deadline. Only slow presets move. There was nothing in the log connecting
the loss to either line, so if you have ever run SF10–SF12 at 31.25 or 62.5 kHz and seen packets simply
not arrive, this is likely why.

Alongside it: a driver that **reports a failed transmit** is now believed. Previously such a packet was
counted as sent, so the statistics disagreed with reality.

## Replies no longer dropped when `flood.max.unscoped` is low

Only affects operators who have lowered `flood.max.unscoped` from its default of 64 — but on those
meshes it looked like repeaters and room servers were ignoring requests. Two separate causes:

- A reply to a **DIRECT** request went out **un-scoped**, because a direct request carries no transport
  codes and there was no fallback. Any repeater on the return path running `flood.max.unscoped=0` then
  dropped it at hop 0. Replies now fall back to the node's own default region scope.
- A repeater answering a **direct login** flooded the reply even when it already had a return path
  stored for that client. It now replies directly along that path.

Ported from upstream MeshCore, with one local difference: ZephCore can tell "the request arrived
un-scoped" apart from "the request was direct", so a requester who reached you un-scoped still gets an
un-scoped reply rather than a scoped one they might not hear.

## New board — Seeed SenseCAP MeshTracker X1

`meshtracker_x1` — nRF52840 + **Semtech LR2021**, 863–928 MHz / 22 dBm, dual-band L1+L5 GNSS,
barometer, haptic motor, RGB LED, buzzer, 1100 mAh battery, 8 MB external flash and an on-board RTC.
Companion, repeater and room server all build and run.

It is essentially a T1000-E with a different radio, so most of the board reuses well-travelled code.
What's new:

- **Barometer support** — a from-scratch SPA06-003 driver. Pressure and temperature are meant to show
  up in normal environment telemetry. **Not yet confirmed on hardware.**
- **Haptic notifications.** The buzzer setting became a four-way notification mode:
  `set buzzer 0|1|2|3` = silent / sound+vibrate / vibrate / sound, and the 3-tap button cycles them.
  Boards without a motor keep the plain on/off toggle. **The X1's motor and buzzer are not yet
  confirmed working** — see Known limitations.
- **8 MB external flash** for contacts, channels and the blob cache, plus the on-board RTC.

## The LR2021 radio is now validated on hardware

Previously this driver was marked experimental with no board behind it. It has now had a full audit and
an on-air validation pass on the X1 — transmit, receive, listen-before-talk and RX duty cycle are all
confirmed working. Highlights of what changed to get there:

- **The chip's firmware patch is now loaded at boot** (Semtech "highly recommends" it; without it the
  chip runs unpatched). The `PRAM loaded:` log line is proof it took.
- **Listen-before-talk runs on the chip itself** — it checks the channel and transmits in one shot with
  no round-trip to the CPU. Longer transmissions automatically fall back to the classic route.
- **RX duty cycle (sniff mode) works**, with the sleep/wake timing derived from the datasheet rather
  than estimated.
- **Several datasheet-conformance bugs fixed**, the most user-visible being a receive filter that was
  not restored after transmitting — a node would silently stop hearing any packet longer than its own
  last transmission.

`promicro_lr2021` still builds but remains untested (the module was destroyed during bring-up).

## LR2021 multi-SF receive — `set extra.sf`

The LR2021 can listen on **up to three extra spreading factors at once**, on the same bandwidth. One
repeater can serve several SF communities without splitting the site.

```
set extra.sf 9 10        # also receive SF9 and SF10
get extra.sf             # -> 9,10
set extra.sf off         # back to single-SF
```

> [!IMPORTANT]
> **Receive only — the bridge it creates is one-way.** Transmission always uses your configured `sf`.
> A node on `sf 7` with `extra.sf 8` hears and forwards SF8 traffic, but forwards it at SF7. So traffic
> flows **SF8 → SF7 and never back**. Adverts and flood traffic propagate fine; an SF8 node's direct
> messages get delivered but its ACKs never arrive, so it retries to its limit every time.
>
> There is no configuration that reverses this — extra SFs must always be *higher* than your main SF,
> and TX always uses the main SF. Treat it as a collector that pulls slower-SF stragglers into a fast
> mesh, not as a link between two SF islands.

The chip's rules are enforced for you, so anything accepted is valid: extra SFs must be higher than
`sf`, distinct, within 4 of each other, and at BW ≥ 500 kHz at most two. A set that no longer fits
after you change `sf` or `bw` is refused at boot and logged. `Error: unsupported` on other radios.

## No more silent mute-until-reboot

All four radio drivers gate transmission on "am I currently receiving?" — and on the LR11xx and LR2021
that answer could get stuck true forever if a packet header arrived but the packet never completed.
The node would keep receiving and never transmit again, with nothing in the log pointing at it. Both
now bound that state with a generous deadline (max-length packet airtime + margin) and log a warning if
it ever fires. The SX126x got the same fix in 1.16.x.

Related: an error-only interrupt on the LR2021 no longer triggers a receive restart that regenerates
the same error, which on the X1 could cost ~88 ms of deafness and a hardware reset.

## Virtual-contact fixes (companion)

The built-in admin chat contact got three fixes worth knowing about:

- **Deleting it in the app is now temporary.** It comes back on the next app connect. Previously a
  "purge all contacts" in the app permanently disabled the feature with no way back except the USB CLI.
  The only durable off switch is `set v.contact off` on the node.
- **Favourite star and telemetry permissions stick** across reconnects and reboots.
- **"Last seen" no longer grows forever**, and the contact re-syncs to the app properly instead of
  being sent exactly once ever.

## Adaptive CAD now uses only levels the chip can distinguish

The auto-tuning channel-detection threshold walks a range of levels, but where those levels fell
outside the radio's own hardware limits, several of them programmed the *identical* setting — so the
tuner was comparing physically identical rungs and reading noise as a trend. The usable window is now
derived per-driver, so every level is a distinct configuration and the `pk` value in `get cad` is what
the chip actually got. This mattered most on the LR2021.

## GPS: dynamic model and a diagnostics readout

- **`CONFIG_ZEPHCORE_GPS_NAV_MODE`** sets the dynamic model on CASIC modules (Quectel L76K / L76KB,
  Air530Z) — stationary for repeaters and room servers, automotive otherwise. Worth setting: **the
  model is stored in the module** and survives reflashing the host, so a second-hand module can arrive
  stuck in an airborne profile and behave oddly on a rooftop.
- **`set gps diag on` / `get gps diag`** shows what the last module-configuration attempt actually did:
  which path ran, bytes sent, and tracked satellites per constellation. Not persisted.

## `set leds on|off` — one master switch

A single persisted switch for every LED on the node, applied live: heartbeat, unread-message and LoRa
TX-activity LEDs, plus the message and shutdown flashes. Works on every role, including headless
repeaters. Display backlight is separate (that's the UI brightness setting).

## Repeater / room-server GPS position is admin-only

Telemetry from a repeater or room server used to hand out its GPS position to anyone holding the
location permission. It now goes to **admins only**. A fixed node that should publish its position can
still do so through prefs and adverts.

## ThinkNode M6 — external flash removed

The M6's datasheet lists a flash chip that the board doesn't have, which almost certainly meant
companion builds never came up on it. The flash node, partition, power regulator and `/ext` mount are
gone; identity, prefs, contacts and channels now all live on internal flash. This is the second M6
datasheet claim disproved by real hardware in as many releases, so treat this board as under-verified
generally.

## Other changes

- **Zephyr pin advanced** past 4.4.2, and **Monocypher updated to 4.0.3**.
- **Sensors behind a switched power rail now come up.** A part on a rail that is powered late cannot be
  probed during early boot, and the rail's declared startup delay never actually runs. Such sensors are
  now initialised on first use instead, once the rail has had the whole boot to settle. This is what
  the X1's barometer and RTC needed; the same handling is now in place for every environment sensor.
- **`get dc.restarts` works on companions.** It reported 0 forever regardless of what the radio was
  doing — and the companion is the role RX duty cycle actually runs in. `clear stats` resets it.
- **ESP32: the user button now wakes the node from light sleep** on boards using the shared PM overlay
  — previously only an incoming packet would.
- **`get freqerr`** (LR2021 only) reports carrier frequency error measured on received packets. Purely
  diagnostic — nothing acts on it, and small values are the expected answer.
- **X1 external flash runs in quad mode**, and the LR2021 SPI layer was resynced with RadioLib.
- Further adaptive-CAD and noise-floor sampling fixes in the SX126x driver.

## Known limitations

- **ESP32 light sleep is validated on one board only** — a Heltec V4, where it works. It is on by
  default for all Heltec repeaters, so the other variants are running it untested. The failure mode to
  watch for is a deaf repeater; check the boot log for `Pin N is not wakeup capable`.
- **ThinkNode M3 / M6 GPS fixes are not confirmed here** — both came from upstream bench data on those
  boards, not measured on ours.
- **`promicro_lr2021` is untested.** The X1 is the only validated LR2021 board.
- **The X1's barometer, haptic motor and buzzer are not validated.** The radio, GNSS, storage, UI and
  power paths are; these three are not. The haptic driver is currently failing to come up at boot
  (`haptic drv2605@5a not ready`) and is under investigation.
- **The X1's on-board RTC does not hold time across a reboot.** Its supply is switched by an MCU pin,
  which goes high-impedance on reset, so the chip raises its power-loss flag and the clock is re-set
  from the next GPS/app/CLI sync. Time is still correct in normal operation; it just isn't preserved
  across a restart.
- **The scoped-reply and transmit-deadline fixes are not on-air validated** — both are build-verified
  and reasoned from the datasheet, not measured on a live mesh.
