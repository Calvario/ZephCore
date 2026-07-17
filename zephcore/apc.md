# Adaptive Power Control (APC) for ZephCore

## Context

TX power is a static user setting (`NodePrefs.tx_power_dbm`, default 22 dBm). When neighbors are nearby and receiving with 20+ dB of excess SNR margin, we're wasting battery and adding unnecessary channel energy. APC automatically reduces TX power when echo packets (dupes of our own transmissions, heard back from neighbors who retransmitted them) indicate strong link margins, and ramps back up when data goes stale (neighbor offline/moving).

This is a novel "echo-based" approach — no published LoRa APC uses this technique. It's well-suited to flood mesh because every retransmit naturally produces echoes without any protocol overhead.

## Status

**Implemented and building** on all boards. **Compiled in by default** (`CONFIG_ZEPHCORE_APC=y`) but **disabled at runtime** — enable per-node with `set tx apc` (persisted in prefs, survives reboot). Works for both companion and repeater roles.

**Rev 2 (2026-07-17):** first field test showed the rev-1 control loop was unusable — see [Control theory](#control-theory-why-rev-1-failed) below. Rev 2 adds reduction self-compensation, true-echo gating, last-hop source attribution, a no-echo fast-recovery tripwire, stale-gap EMA re-warmup, and SF5/6 thresholds. Bench/field validation of rev 2 is pending.

## Control theory (why rev 1 failed)

**The echo SNR we measure does not respond to our own TX power.** An echo's SNR is set by the *neighbor's* transmitter power and the path loss — reducing our power changes what *they* receive, not what *we* measure. Rev 1 compared this unresponsive measurement against the target and stepped power down whenever margin looked high, so in any healthy neighborhood it ratcheted straight to −12 dB and pinned there. The only feedback that ever closed the loop was catastrophic:

- **Companion:** reduce until nobody decodes us → echoes stop → EMA crashes → power back up → repeat. A limit cycle hunting the audibility cliff, losing real messages in every trough.
- **Repeater:** peers echo the *origin's* copy whether or not they can hear us, so echoes never even stop — the node just sat at −12 dB while its actual forwarding coverage shrank.

(The rev-1 design implicitly assumed a network where every node runs APC — then each node's reduction lowers the *others'* measured margins and a coupled equilibrium exists. On a real mesh of vanilla nodes there is no such coupling.)

**Rev 2 fix — self-compensation.** By reciprocity, the neighbor hears us at approximately `echo_margin − our_reduction` (when TX powers are comparable). The controller now regulates that predicted value:

```
excess = margin_ema − target − current_reduction
excess > +1 dB  → increase reduction by min(3, excess) per tick
excess < −1 dB  → decrease reduction by min(6, −excess) per tick
```

This has a genuine equilibrium (`reduction ≈ margin_ema − target`), converges in a few 5 s ticks, and works unilaterally — no other node needs APC. In an all-APC network, mutual compensation double-counts and both sides settle *above* target (safe, slightly less savings).

## True echoes (what counts as evidence)

A dupe only proves our TX was received if it actually **routed through us**:

- **Originated floods** (`path_pos = ORIGINATED`): nobody else had the content before our TX, so every dupe is a true echo.
- **Forwarded floods**: when we retransmit, we append our path hash at index `n`. A true echo carries our hash at exactly that position. Parallel retransmits by peers who heard the origin (including dupes heard *before our own TX airs* — tracking happens at queue time and the contention window can hold our TX for ~2 s) don't carry it and are rejected.

Consequences of the gate:

- The margin EMA measures the link to nodes **for whom we are upstream** — exactly the constituency that depends on our TX.
- Where nobody depends on us (edge repeater, quiet leaf area), true echoes are rare → staleness holds power at max. Conservative and correct.
- With 1-byte path hashes a false positive is a 1/256-per-dupe collision; the EMA absorbs it.

The echo **source** is the *last* path entry (`path[count−1]` — the node that just transmitted this copy; every retransmitting node appends itself). Rev 1 keyed on `path[0]`, the origin's first hop, which is identical for every echo of a forwarded flood — that collapsed all echoers into one max-SNR source and disabled the rogue clustering entirely.

## Important: Link Asymmetry

APC measures the **return path** SNR (neighbor → us), not our outgoing SNR (us → neighbor). These differ when nodes have mismatched hardware — especially nodes with poor RX sensitivity ("bad ears") — or mismatched TX power (a 30 dBm neighbor overstates what it hears from our 22 dBm ceiling by 8 dB).

**Path loss is reciprocal** (same frequency, same physical path), so echo SNR is a good proxy for link quality in most cases. The target margin provides a safety buffer for hardware asymmetry.

**If your network has nodes with poor RX hardware or boosted TX power**, increase the target margin:
- Default: 16 dB (good for networks with similar hardware)
- 20-22 dB: recommended for mixed hardware networks
- 24-30 dB: very conservative, for networks with known bad receivers

See [CLI Commands](#cli-commands) for how to change the margin at runtime.

## Architecture

### Class: `mesh::PowerController`

Follows the `ContentionTracker` pattern: static ring buffer, EMA, `tick()` from maintenance loop.

**File:** `include/mesh/PowerController.h`, `src/PowerController.cpp`

```
PowerController
  _ring[16]            <- tracks recently sent packets (FNV-1a hash)
  _margin_ema_x256     <- EMA of link margin (SNR - SF_threshold), fixed-point
  _power_reduction_db  <- current TX power reduction (0 to MAX_REDUCTION)
  _target_margin_x4    <- configurable target margin (default 64 = 16 dB)
  _enabled             <- runtime enable/disable (object defaults true; each role
                          applies prefs.apc_enabled at begin() — prefs default 0 = off)
  _last_echo_ms        <- timestamp of most recent true echo (staleness / re-warmup)
  _noecho_streak       <- consecutive unechoed TX (fast-recovery tripwire)
  _echo_count/_noecho_count <- finalized-entry diagnostics (CLI)
  _sf                  <- current spreading factor (for threshold lookup)
```

**Constants:**
| Parameter | Value | Rationale |
|-----------|-------|-----------|
| RING_SIZE | 16 | Match ContentionTracker; handles ~3 pkts/s with 5s window |
| ECHO_WINDOW_MS | 10,000 ms | 10s: covers SF12 2-hop echoes (~7s airtime + processing). Window starts at queue time, so a contention-delayed retransmit effectively gets ~8s post-TX |
| STALE_MS | 120,000 ms (2 min) | Mobile mesh — neighbors move/die fast |
| EMA_SHIFT | 2 (alpha=1/4) | More responsive than contention's 1/8 |
| WARMUP_COUNT | 3 | Need a few echoes before acting |
| MAX_SOURCES | 3 | Track up to 3 distinct echo sources per packet |
| STEP_DOWN_DB | 3 | Max reduction increase per tick (~halving power), conservative |
| STEP_UP_DB | 6 | Max reduction decrease per tick — power restores 2x faster than it drops |
| MAX_REDUCTION_DB | 12 | Floor at 10 dBm (from 22 max) |
| CLUSTER_WIDTH_X4 | 24 | 6 dB in x4 units — echo SNRs within 6 dB of best are clustered |
| DEFAULT_TARGET_MARGIN_X4 | 64 | 16 dB above SF sensitivity (configurable at runtime) |
| HYSTERESIS_X4 | 4 | 1 dB deadband around `margin_ema − target − reduction = 0` |
| NOECHO_TRIP_COUNT | 2 | Consecutive unechoed TX (while reduced) that restore full power immediately |
| PATH_POS_ORIGINATED | 0xFF | `path_pos` sentinel: we originated, any dupe is a true echo |

**Public API:**
- `setEnabled(bool en)` / `isEnabled()` — runtime enable/disable
- `setSF(uint8_t sf)` — set current SF for margin calculation
- `setTargetMargin(uint8_t margin_db)` / `getTargetMargin()` — configure target link margin (default 16 dB)
- `trackTransmit(uint32_t hash32, uint32_t now_ms, uint8_t path_pos)` — called when we queue a flood for TX; `path_pos` = index of our appended hash, or `PATH_POS_ORIGINATED`
- `recordEcho(uint32_t hash32, int8_t snr_x4, uint32_t now_ms, const uint8_t *path, uint8_t path_count, uint8_t hash_size, const uint8_t *self_hash)` — called on every flood dupe; true-echo gate + last-hop attribution + per-source best SNR; returns true if the dupe matched and passed the gate
- `tick(uint32_t now_ms)` — finalize expired entries into EMA, run the compensated control step, handle staleness
- `getPowerReduction() const` -> `int8_t` (0 to MAX_REDUCTION_DB; returns 0 when disabled)
- `getMarginEstimate() const` -> `float` (dB, for diagnostics)
- `getEchoCount()` / `getNoEchoCount()` — finalized-entry counters (diagnostics)
- `getLastSourceCount() const` -> `uint8_t` (echo source count from most recent entry)
- `isWarmedUp() const` / `isStale(uint32_t now_ms) const`

**Per-source SNR tracking in EchoEntry:**
```cpp
struct EchoEntry {
    uint32_t hash32;
    uint32_t timestamp_ms;
    uint8_t  source_count;
    uint8_t  sf_at_track;       /* SF when packet was transmitted */
    uint8_t  path_pos;          /* our path index, or PATH_POS_ORIGINATED */
    int8_t   reduction_at_track;/* power reduction in effect at track time */
    Source sources[MAX_SOURCES];
    bool     active;
};
```

Each entry stores the SF at track time (`sf_at_track`) so that margin calculation uses the correct threshold even if the radio SF changes while entries are in-flight, and the reduction at track time (`reduction_at_track`) for the no-echo encoding below.

**`recordEcho` logic:**
1. Find matching entry by hash32
2. Check if entry has expired (beyond ECHO_WINDOW_MS) — if so, finalize and reject
3. **True-echo gate**: if the entry is a forwarded flood (`path_pos != ORIGINATED`), require `path_count > path_pos` and `path[path_pos] == self_hash` — else reject
4. Derive the source: `src = path[count−1]` (the node that transmitted this copy)
5. **Stale-gap re-warmup**: if the previous echo was > STALE_MS ago, reset the EMA and warmup counter — an EMA from before the gap must be re-earned, one fresh echo must not re-apply old reduction
6. Update the source's SNR if better, or add a new source (up to MAX_SOURCES)
7. Update `_last_echo_ms`

**Computing "robust SNR" when finalizing an entry (1+ sources):**
1. **1 source**: use its SNR directly (no rogue detection possible, and no need)
2. **2-3 sources**: sort descending, cluster within CLUSTER_WIDTH (6 dB) of the best:
   - If 2+ in cluster -> median the cluster values (2: average, 3: middle)
   - If only 1 in cluster (top value is isolated = rogue) -> drop it, use next source(s)

**No-echo entries** (0 sources): nobody downstream decoded the TX. At the reduction R in effect when it was sent, that bounds the full-power margin at `margin ≤ R` — the sample is encoded as `margin = R` (the least pessimistic consistent value). Occasional misses (collision, RX duty cycle missing the echo) therefore nudge the EMA down gently instead of crashing it; real link loss is handled by the tripwire:

**Fast-recovery tripwire:** `NOECHO_TRIP_COUNT` (2) consecutive unechoed transmissions while reduction > 0 → reduction slams to 0, EMA and warmup reset (re-earn from fresh samples). A mobile node that walked away from its neighbor recovers full power after two lost floods instead of waiting ~40 s of EMA lag.

**Power adjustment algorithm (in `tick()`):**
1. Finalize expired entries into the EMA (x256 fixed-point, warmup seeding for first 3 entries)
2. **Staleness takes priority** (mutually exclusive with the control step):
   - If `now − _last_echo_ms > STALE_MS (2 min)` -> ramp reduction toward 0 by STEP_UP_DB (6) per tick (full recovery from max in ~10 s)
   - When stale, **never increase reduction** — old EMA data is unreliable
3. Otherwise run the compensated control step:
   - `excess = margin_ema − target − reduction` (all x256)
   - `excess > +1 dB` -> reduction += min(STEP_DOWN_DB, excess)
   - `excess < −1 dB` -> reduction −= min(STEP_UP_DB, −excess)
   - inside the deadband -> hold
   - clamp to [0, MAX_REDUCTION_DB]

### Integration points (all guarded by `#ifdef CONFIG_ZEPHCORE_APC`)

**1. Track originated packets** — `src/Mesh.cpp` `sendFlood()` (both overloads)

After `_tables->markSeen(packet)`:
```cpp
uint32_t h = ContentionTracker::computePacketHash32(packet);
_power_ctrl.trackTransmit(h, (uint32_t)_ms->getMillis(),
                          PowerController::PATH_POS_ORIGINATED);
```

**2. Track retransmitted packets** — `src/Mesh.cpp` `routeRecvPacket()`

Alongside existing `_contention.trackRetransmit()`, after appending our hash at path index `n`:
```cpp
_power_ctrl.trackTransmit(h, (uint32_t)_ms->getMillis(), n);
```

**3. Record echoes** — `src/Mesh.cpp` `onRecvPacket()`

In the flood dupe detection block, passing the raw path for gating + attribution:
```cpp
uint8_t hs = pkt->getPathHashSize();
uint8_t self_hash[4];
self_id.copyHashTo(self_hash, hs);
_power_ctrl.recordEcho(h, pkt->_snr, (uint32_t)_ms->getMillis(),
                       pkt->path, pkt->getPathHashCount(), hs, self_hash);
```

**4. Tick + propagate to radio** — `src/Mesh.cpp` `maintenanceLoop()`

```cpp
_power_ctrl.tick(now);
_radio->setTxPowerReduction(_power_ctrl.getPowerReduction());
```

**5. Apply power reduction** — `adapters/radio/LoRaRadioBase.cpp` `buildModemConfig()`

After existing TX power clamps:
```cpp
cfg.tx_power -= _tx_power_reduction_db;
if (cfg.tx_power < -9) cfg.tx_power = -9;
```

The config cache (`configParamsEqual`) already compares `tx_power`, so a changed reduction naturally triggers `hwConfigure()` on next TX — no explicit reconfigure needed.

**6. Set SF** — `RepeaterMesh::begin()`, `RoomServerMesh::begin()` and `CompanionMesh::begin()` / BLE radio param change callbacks. (`tempradio` overrides do NOT update APC's SF — a temp session at a different SF computes margins against the wrong threshold; acceptable for a short-lived diagnostic mode.)

### Radio interface

Virtual APC methods added to `mesh::Radio` (base class):
```cpp
virtual void setTxPowerReduction(int8_t reduction_db) { (void)reduction_db; }
virtual int8_t getTxPowerReduction() const { return 0; }
```

`LoRaRadioBase` overrides these with a stored `_tx_power_reduction_db` member.

### Kconfig

In `Kconfig` under "LoRa Power Saving" menu:

```kconfig
config ZEPHCORE_APC
    bool "Adaptive Power Control (APC)"
    default y
```

**Compiled in by default, disabled at runtime.** The `apc_enabled` pref (default 0)
gates it per-node; `set tx apc` enables and persists. Build with
`-DCONFIG_ZEPHCORE_APC=n` to exclude the code entirely (zero overhead). Works for
both companion and repeater roles.

### CLI commands

**`get tx`** — shows current APC state:
```
> 16dBm (apc=on max=22 reduction=6 margin=18.5 target=16)   # APC enabled
> 22dBm (apc=off)                                            # APC disabled
```

**`get tx apc`** — diagnostics-first form with echo counters:
```
> apc=on effective=16dBm max=22 reduction=6 margin=18.5 target=16 echo=123 noecho=4
> apc=off max=22dBm target=16
```
`echo`/`noecho` count finalized tracked transmissions with/without at least one true
echo — the noecho:echo ratio is the first thing to check when APC misbehaves in the
field (high noecho = nobody downstream, or gating rejecting everything).

**`get apc.margin`** — shows current target margin:
```
> 16 dB
```

**`set tx apc`** — enables APC (persisted).
```
OK - tx power=22 dBm (apc=on)
```

**`set tx <number>`** — disables APC and sets fixed TX power:
```
OK - tx power=16 dBm (apc=off)
```

**`set apc.margin <dB>`** — set APC target link margin (range 6-30 dB, persisted):
```
OK - APC target margin=20 dB
```

The user's TX power setting (`NodePrefs.tx_power_dbm`) is always the ceiling — APC only subtracts from it. Disabling clears APC runtime state; re-enabling starts from a fresh warmup.

**Note:** Both the APC enable state (`apc_enabled`) and the target margin (`apc_margin`) are persisted in prefs and survive reboots. The companion default margin is 20 dB (mobile, set in `main_companion.cpp`); repeater default is 16 dB.

### Target margin — what it means and how to choose

The target margin is the SNR margin APC tries to maintain **at the downstream neighbor's receiver** (predicted as `echo_margin − reduction`), above the minimum required for reliable reception at the current SF.

At equilibrium `reduction ≈ margin_ema − target`, so with echo margin 26 dB and target 16, reduction settles around 9-10 dB and the neighbor keeps ~16-17 dB of real margin.

**Example scenarios:**

**Scenario 1: Two good radios on a rooftop, 500m apart**
Echo SNR = +15 dB at SF8 (threshold −10) → margin_ema ≈ 25 dB.
Reduction converges to ~8-9 dB over three ticks (3 dB/tick, 1 dB deadband).
The echo SNR we measure stays +15 (it's their TX power, not ours); the
*neighbor* now hears us ~16-17 dB above threshold. Stable — no further movement.

**Scenario 2: Good radio talking to a cheap node with -5 dB RX degradation**
You hear the echo at +15 dB, but the cheap node only hears you at +10 dB.
With default margin (16 dB): APC reduction ≈ 9; the cheap node really sees ~7-8 dB
above its threshold. Fading may bite. `set apc.margin 21` shifts the buffer to
cover the asymmetry.

**Scenario 3: Your network has radios with 10+ dB RX variation**
Some nodes have external LNAs (+3 dB), others have bad antennas (-7 dB).
Total asymmetry up to 10 dB. Set `set apc.margin 22` to ensure the worst
receiver still gets 12 dB of real margin after APC reduces power.

**Rule of thumb:**
- Default (16 dB): most networks
- Add the worst-case RX/TX asymmetry in your network to 16 dB
- If you don't know: 20 dB is a safe middle ground (the companion default)

## What APC does NOT see

- **Zero-hop packets** (local advertisements): not retransmitted, no echo. APC only tracks flood packets.
- **Outgoing SNR**: APC measures return-path SNR and predicts the outgoing margin via reciprocity + self-compensation. Hardware/TX-power asymmetry is covered only by the target-margin buffer.
- **Silent leaves**: a receive-only node that never transmits produces no echoes under any scheme. The target margin plus the 12 dB reduction cap is their only protection.
- **Per-neighbor granularity**: APC produces a single global power reduction. It does not adjust power per destination — the radio can only set one TX power at a time.
- **Echoes missed by RX duty cycle**: sniff mode can sleep through an echo; the miss is encoded conservatively (see no-echo policy) but adds noise. Expect slightly less reduction with `rx_duty_cycle` on.

## Files created/modified

| File | Action |
|------|--------|
| `include/mesh/PowerController.h` | **CREATE** — PowerController class |
| `src/PowerController.cpp` | **CREATE** — implementation |
| `include/mesh/Radio.h` | EDIT — added virtual `setTxPowerReduction`/`getTxPowerReduction` |
| `include/mesh/Mesh.h` | EDIT — added `_power_ctrl` member + accessors |
| `src/Mesh.cpp` | EDIT — 4 integration points |
| `adapters/radio/LoRaRadioBase.h` | EDIT — added `_tx_power_reduction_db` + override methods |
| `adapters/radio/LoRaRadioBase.cpp` | EDIT — apply reduction in `buildModemConfig()`, init member |
| `Kconfig` | EDIT — added `ZEPHCORE_APC` option |
| `CMakeLists.txt` | EDIT — conditional compile of `PowerController.cpp` |
| `helpers/CommonCLI.h` | EDIT — APC callbacks (`getAPCReduction`, `getAPCMargin`, `isAPCEnabled`, `setAPCEnabled`, `getAPCTargetMargin`, `setAPCTargetMargin`, `getAPCEchoCount`, `getAPCNoEchoCount`) |
| `helpers/CommonCLI.cpp` | EDIT — `get tx`, `get tx apc`, `get/set apc.margin`, `set tx` APC enable/disable |
| `app/RepeaterMesh.h` / `app/RoomServerMesh.h` | EDIT — APC callback overrides |
| `app/RepeaterMesh.cpp` / `app/RoomServerMesh.cpp` | EDIT — `_power_ctrl` setup in `begin()` |
| `app/CompanionMesh.h` / `app/CompanionMesh.cpp` | EDIT — APC hooks + `begin()`/BLE param setup |
| `src/main_companion.cpp` | EDIT — CLI callback wiring, companion margin default 20 |

## Verification

1. **Build test**: `west build -b rak4631 zephcore --pristine` and `west build -b wio_tracker_l1 zephcore --pristine` — both pass
2. **Kconfig disable**: Build with `-DCONFIG_ZEPHCORE_APC=n` — zero overhead
3. **CLI**: Flash a repeater, run `get tx` — should show `> 22dBm (apc=off)` initially (runtime default is off); after `set tx apc`, `> 22dBm (apc=on max=22 reduction=0 margin=0.0 target=16)`
4. **Functional**: Two nodes in close proximity (high SNR) exchanging flood traffic through a third (echo source). After a few exchanges, `get tx apc` should show reduction converging to ~`margin − target`, then holding (no ratcheting to max, no oscillation).
5. **Gating sanity**: on a repeater in a dense area, `noecho` counting up while `echo` stays low means no downstream nodes — reduction should stay near 0. That is correct behavior, not a bug.
6. **Staleness**: Power off the echoing neighbor. Within ~2 minutes, `get tx apc` should show reduction ramping to 0 (6 dB per 5 s tick).
7. **Fast recovery**: with reduction active, kill the echo path and send two floods — reduction should drop to 0 immediately after the second unechoed window expires (~20 s), not wait for the 2-min staleness.
8. **Override**: `set tx 16` disables APC and fixes power. `set tx apc` re-enables.
9. **Margin config**: `set apc.margin 20` changes the target. `get apc.margin` confirms.
10. **Logging**: `CONFIG_ZEPHCORE_MAIN_LOG_LEVEL_DBG` shows per-entry finalization; reduction changes, tripwire and stale-gap re-warmup log at INF.

## Resolved decisions

1. **No-echo encoding**: an unechoed TX at reduction R samples as `margin = R` (bounds the full-power margin from above). Gentle correction for occasional misses; the 2-strike tripwire handles real link loss. (Rev 1 used margin=0 unconditionally, which crashes the EMA on a single collision.)
2. **Staleness**: 2-minute timeout, recovery at STEP_UP_DB (6) per tick — full recovery from max reduction in ~10 s once triggered. When stale, APC never increases reduction. On the first echo after a stale gap, the EMA and warmup reset — old data cannot re-apply reduction.
3. **Max reduction**: 12 dB. Combined with the compensated law it is only reached when echo margin ≥ target + 12.
4. **Primary target**: SF8/BW62.5 (SNR threshold -10.0 dB). Threshold table covers SF5-SF12.
5. **Both roles**: APC is active for companions, repeaters and room servers.
6. **Rogue filtering via SNR clustering**: with 2+ distinct echo sources, cluster within 6 dB of the best; an isolated top outlier is dropped. Source identity = last path entry (the actual transmitter of the echo copy).
7. **Per-entry SF tracking**: margin calculation uses `sfThresholdX4(sf_at_track)` so SF changes mid-flight don't corrupt margins.
8. **Echo window**: 10 s (covers SF12 2-hop echoes at ~7 s; starts at queue time).
9. **CLI override**: `set tx <number>` disables APC and sets fixed power; `set tx apc` re-enables (fresh warmup — disable clears runtime state).
10. **Configurable target margin**: `set apc.margin <6-30>`, persisted. Default 16 dB (repeater), 20 dB (companion).
11. **True-echo gating**: forwarded floods only count dupes carrying our path hash at the position we appended it; originated floods count any dupe. This is what makes the no-echo signal honest on repeaters (rev 1 counted parallel retransmits of the origin's copy, including dupes heard before our own TX aired).
12. **Companion APC**: No app changes needed — the app's TX power setting changes the ceiling, APC subtracts from it. The companion applies `prefs.apc_enabled` / `prefs.apc_margin` at `begin()`.
