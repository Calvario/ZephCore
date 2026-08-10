# ZephCore 1.17.0-zephcore

> [!IMPORTANT]
> ## Before you upgrade
>
> **From v1.16.5 – v1.16.8** — clean flash, no re-bond, bonds and data survive.
>
> **From v1.16.2 / v1.16.3 / v1.16.4** — clean flash, bonds and data survive. If you are on an
> **ESP32-S3 / ESP32-C board** and have *not* yet taken the v1.16.5 update, you still owe the one-time
> serial reflash: the app moved to flash offset `0x10000` in v1.16.5, and a board on the old layout
> cannot cross that update over WiFi-OTA or the browser flasher — **flash the `-merged.bin` once over
> USB/serial.** Affected boards: *Heltec V3 / V4 / V4.3, Station G2, Wireless Tracker / V2, XIAO
> ESP32-S3 / C3 / C6, LilyGo T-Lora C6, ThinkNode M9.* Identity, contacts, channels, prefs, and BLE
> bonds are preserved. nRF52, classic ESP32 (T-Beam / PICO-D4 / TTGO LoRa32), STM32WL, and native
> Linux are unaffected.
>
> **From v1.16.1 or older** — flash it; on first boot it clears BLE bonds automatically (identity,
> contacts, channels, prefs preserved). Re-bond your phone/desktop once.
>
> **Coming from Arduino MeshCore** — flash it; auto-formats on first boot (new identity, clean storage).
>
> **ThinkNode M6 owners: this release stops using the external flash chip, because the board does not
> have one.** Contacts, channels and prefs move to internal flash on first boot. See the M6 section.
>
> **Repeater and room-server telemetry no longer hands out your GPS position to non-admins.**
> If you were relying on public position reporting from a fixed node, set the location in prefs and
> advertise it instead.
>
> **ESP32 light sleep (new in 1.16.8) is still awaiting hardware validation.** It remains on by default
> for Heltec repeaters, and their USB CLI still stops answering 10 minutes after boot as a result —
> reconnecting the terminal resets the node and gives you another window. Rebuild with `-DCONFIG_PM=n`
> to take it out.

---

A **hardware and receive-path release**. A new tracker board lands (Seeed SenseCAP MeshTracker X1, the
first LR2021 target with real hardware behind it), the LR2021 gains multi-SF receive, the LR11xx and
LR20xx receive gates get the same stuck-state bound the SX126x got in 1.16.x, GPS module configuration
grew a diagnostics path and a dynamic-model setting, and the Zephyr pin moves to 4.4.2 final.

## Highlights

### New board — Seeed SenseCAP MeshTracker X1

`meshtracker_x1` — nRF52840 + **Semtech LR2021**, 863–928 MHz / 22 dBm, dual-band L1+L5 Airoha GNSS,
SPA06-003 barometer, DRV2605L haptic motor, RGB LED, buzzer, 1100 mAh high-voltage LiPo, 8 MB QSPI
flash and an on-board RTC. Companion, repeater and room-server all build.

Hardware-wise it is a T1000-E with the radio swapped, so the LoRa SPI pins, button, buzzer, battery
divider and the whole GNSS control pin set map straight onto the existing T1000-E paths. What is new
on this board:

- **SPA06-003 barometer driver, written from scratch** (`adapters/sensors/spa06.c` plus its binding) —
  no upstream Zephyr driver exists. It lives in the app tree rather than the Zephyr tree, so it needs
  no patch and survives `west update`. Pressure and temperature are reported through the normal
  environment-telemetry path.
- **Haptic notifications.** `helpers/ui/haptic.c` wraps Zephyr's `ti,drv2605`, and the buzzer setting
  became a four-state notification mode — `set buzzer 0|1|2|3` = silent / sound+vibrate / vibrate /
  sound, with the 3-tap button cycling through them. Boards with no motor keep a plain on/off toggle.
  The value rides in the existing `buzzer_quiet` byte, so there is no prefs migration.
- **8 MB of QSPI flash as `/ext`** for contacts, channels and the blob cache, and the on-board
  YSN8900 RTC on its own I²C bus, discovered and BCD-validated at boot.

> [!NOTE]
> The **LR2021 radio path is still bring-up code.** The driver is ZephCore's own, and only one LR2021 board
> has been validated on air yet. Treat X1 radio behaviour as untested; the rest of the board (GNSS,
> sensors, storage, UI, power) is on well-travelled paths.

### LR2021 multi-SF receive — `set extra.sf`

The LR2021 can demodulate up to **three extra spreading factors at once**, alongside the configured
one, on the same bandwidth. One repeater can therefore serve several SF communities without splitting
the site, and which SF a packet arrived on is a chip-side readout rather than an inference.

```
set extra.sf 9 10        # receive SF9 and SF10 in addition to the configured SF
get extra.sf             # -> 9,10
set extra.sf off         # back to single-SF receive
```

> [!IMPORTANT]
> **Receive only, and the bridge it creates is one-way.** Transmission always uses the single
> configured `sf`, and every detector shares one bandwidth — multi-SF, not multi-channel.
>
> A node running `sf 7` with `extra.sf 8` hears SF8 traffic and *does* forward it, but the forward goes
> out at SF7. Traffic therefore moves **SF8 → SF7 only, and nothing comes back**. Adverts and one-way
> flood traffic propagate fine; an SF8 node's direct messages get delivered but its ACKs never arrive,
> so it retries to its limit every time. That is worth weighing before deploying it next to a busy
> slow-SF island.
>
> The direction cannot be reversed. Because every extra SF must be *greater* than `sf`, the main SF is
> always the lowest in the set, and TX always uses the main SF — so a side-detector node is always
> high-SF-in / low-SF-out. Two of them back to back point the same way; **no configuration carries
> SF7 → SF8.** Think of this as a collector that pulls slower-SF stragglers into a fast mesh, not as a
> link between two SF islands. A genuine bidirectional bridge needs two nodes joined by a non-LoRa
> link, or one node time-slicing its radio config — neither of which this feature provides.

The chip's constraints are enforced in the driver, so an accepted set is a valid one: every extra SF
must be **greater** than the configured `sf`, all distinct, highest−lowest ≤ 4, and at BW ≥ 500 kHz at
most two (only one when `sf` ≥ 10). The setting is persisted and re-applied on every RX entry; a set
that no longer fits after an `sf` or `bw` change is refused at boot and logged rather than silently
half-applied. `Error: unsupported` on every non-LR2021 radio.

One thing worth knowing about how this interacts with listen-before-talk: the chip's SF constraint for
**CAD** is the exact inverse of the one for **receive** — CAD wants the main SF above the side SFs,
receive wants it below. Both cannot hold at once, so the driver switches side detectors off for each
LBT CAD and restores them when RX re-arms. That costs two extra SPI commands per transmission and
needs no configuration. (Arduino MeshCore hit the same wall from the other direction, as a `-706`
error on the receive restart after a hardware CAD.)

### The LR11xx receive gate can no longer wedge

The driver reports "we are currently receiving" from the chip's latched PREAMBLE_DETECTED and
HEADER_VALID bits, and that answer is what gates every transmission. Neither bit is self-clearing:
they are dropped only by a terminal interrupt or an RX restart, and in continuous receive there is no
symbol timer, so neither is guaranteed to arrive. A header whose packet never completes would pin the
gate true **forever** — the node keeps receiving but never transmits again, until reboot, with nothing
in the log pointing at it.

This is the same class of fault the SX126x driver was hardened against earlier (`0013`), and it is now
closed on the LR11xx:

- **LR11xx (LR1110)** already had the preamble grace; it gains the payload deadline.
- **LR20xx (LR2021)** still has an unbounded gate — a bare OR of the two bits. The bound was written
  for it too, then pulled back out: it landed alongside a duty-cycle rework that regressed receive on
  the MeshTracker X1, and the whole group was reverted to the last configuration confirmed working on
  hardware. It will return once the LR2021 receive path is settled.

The deadline is the airtime of a maximum-length packet at the current SF/BW at coding rate 4/8 with
LDRO pinned on, plus 25% and 100 ms — deliberately generous, because this is a stuck-state safety net
and releasing early would start a transmission on top of a packet that is still arriving. When it
fires it logs a warning, which is the tripwire that would prove the fault happens in the field; it has
never been reproduced on a bench.

### ThinkNode M6 — the external flash is gone, because the chip is not there

The M6 routes a QSPI bus and its datasheet lists an MX25R1635F, so ZephCore declared the part, gave it
a power-enable regulator, and mounted it as `/ext`. Upstream Arduino MeshCore has since dropped the
same assumption after hitting it on real hardware — *"M6 doesn't have flash chip available, so
companion roles fail to start"* — which means companion builds most likely never came up on this board
here either.

The flash node, its partition, the enable regulator and the `/ext` mount are removed. Everything now
lives on internal flash: identity, prefs, contacts and channels all on `/lfs`. This is the second
ThinkNode M6 assumption disproved by upstream bench data in as many releases — the GPS REINIT pin was
the first — so the board is worth treating as under-verified generally.

### GPS module configuration: dynamic model, and a diagnostics readout

Two additions on the GNSS side, both aimed at the thing that is hardest to see from outside: whether
the module actually accepted what the firmware sent it.

- **`CONFIG_ZEPHCORE_GPS_NAV_MODE`** sets the dynamic model on CASIC-family modules (Quectel L76K /
  L76KB, Air530Z) — stationary by default for repeaters and room servers, automotive otherwise. This
  is worth setting rather than leaving alone, because **the model is stored in the module** and
  survives reflashing the host: a module pulled from another device can arrive stuck in an airborne
  profile and behave oddly on a rooftop.
- **`set gps diag on` / `get gps diag`** reports what the last module-configuration attempt actually
  did: which path ran, how many bytes were sent, and tracked satellites per constellation. Not
  persisted — it clears on reboot.

### `set leds on|off` — one master switch

A single persisted switch for every LED on the node, applied live: heartbeat, unread-message and LoRa
TX-activity LEDs, plus the message and shutdown flashes. It works on every role, including headless
repeaters where the TX LED is the only one that ever lights. The display backlight is not covered —
that is the separate UI brightness setting.

### Repeater and room-server GPS position is now admin-only

Telemetry from a repeater or room server used to include the node's GPS position for any requester
holding the location permission. It is now released to **admins only**. A fixed node that should
publish its position can still do so through prefs and adverts, which is the deliberate, visible way
to share it.

## Other changes

- **Zephyr pinned to 4.4.2 final** (was a 4.4.2 pre-release commit).
- **ESP32: the user button is now a light-sleep wake source** on boards using the shared PM overlay, so
  a sleeping node wakes on a button press rather than only on a received packet.
- **LR2021 SPI HAL resynced with RadioLib**, and the driver picked up a batch of datasheet-conformance
  fixes: the TCXO start-up deadline is counted in 32 MHz periods rather than RTC ticks (the previous
  unit turned a 5 ms allowance into 5 µs and guaranteed a start-up error), front-end calibration is
  issued in its full three-slot form, the DIO interrupt mask is narrowed to the events the handler
  actually services, the CAD detection thresholds now come from the datasheet's 4-symbol row to match
  the 4-symbol window actually used, and the receive payload-length filter is restored after each
  transmission — without that, a node silently stopped hearing any packet longer than its own last
  transmission.
- **Adaptive-CAD and noise-floor sampling** picked up further fixes in the SX126x patch set.

## Known limitations

- **LR2021 is not hardware-validated.** No LR2021 board has been confirmed on air. The X1's radio path
  and everything in the multi-SF feature above are code-complete and build-verified only.
- **ESP32 light sleep is not hardware-validated** and remains on by default for Heltec repeaters. The
  failure mode to rule out is a deaf repeater; check the boot log for `Pin N is not wakeup capable`.
- **ThinkNode M3 / M6 GPS fixes are not hardware-confirmed** — both were derived from upstream bench
  data on their respective boards, not measured here.
