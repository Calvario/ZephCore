# LR2021 CAD changes — build, flash, measure

Local-only. Produced 2026-08-12 alongside the Batch 1 work from
`HANDOVER_lr2021_audit.md`. All paths are relative to the west workspace root
(`D:\zephcore`), which is also the git root.

## What is in the tree right now

Batch 1 **plus** the combined CAD change:

- **CAD_LBT is the normal route.** `CadExitMode = 0x10` (DS Table 6-18) — the
  chip runs the CAD and, on a clear channel, goes straight to Tx with no host
  round-trip. The old two-step CAD → host → `SetTx` path survives **only** as
  the fallback above the 524 ms ceiling (see below).
- **Fast CAD**, `pnr_delta = 8`. DS §6.3.11: "CadDone time is shortened in case
  of early no detection". Most LBT CADs find nothing, so this shortens the
  common pre-TX path.
- **CAD → carrier-up latency instrumentation** (`CAD->TX latency: NNNN us`).

`lr2021_bench/cad_lbt_fast.patch` is that whole thing as one unit, generated
against Batch 1.

### Committing it separately

Do not use `git add -p` — the hunks interleave with Batch 1 in the same file.
Revert, commit Batch 1, re-apply, commit the CAD change:

```bash
git apply -R lr2021_bench/cad_lbt_fast.patch
```

Commit Batch 1, then:

```bash
git apply lr2021_bench/cad_lbt_fast.patch
```

## Build

Always with `debug.conf` — the verification lines below are compiled out
otherwise.

```bash
west build -b meshtracker_x1 zephcore --pristine -d build_all_x1 -- -DEXTRA_CONF_FILE="boards/common/debug.conf"
```

| Build | FLASH | RAM |
|---|---|---|
| Gate 0 (untouched) | 475,844 B / 68.74 % | 240,424 B / 91.71 % |
| Batch 1 (`build_b1_x1`) | 476,240 B / 68.80 % | 240,424 B / 91.71 % |
| Batch 1 + CAD (`build_all_x1`) | 476,996 B / 68.91 % | 240,424 B / 91.71 % |

RAM is unchanged throughout; the debug build's ~15.9 KB of headroom is intact.

## Flash and read it — no J-Link needed

`debug.conf`'s "RTT backend only" comment is stale. The generated config has
`CONFIG_LOG_BACKEND_UART=y` and the X1's chosen console is `cdc_acm_uart`, so
**logs come out over USB CDC**. One port carries both the log stream and the
CLI: binary companion frames are prefixed with `<`, and bare text lines go to
`CommonCLI`. So a plain serial terminal gives you logs and `get cad` in the
same window.

Double-tap reset, then drag the UF2:

```bash
explorer /select,"D:\zephcore\build_all_x1\zephyr\zephyr.uf2"
```

Find the port after it re-enumerates:

```bash
powershell -Command "Get-CimInstance Win32_SerialPort | Select-Object DeviceID,Description"
```

Then attach any terminal at 115200 (the rate is ignored by CDC). If you have
`pyserial`:

```bash
python -m serial.tools.miniterm COM7 115200
```

## What to look for

### Batch 1

- `PRAM loaded: type=0x03 version=0x13 (560 words)` — still present; both reset
  paths reload it. Absent or an error line is a **Gate 0 failure**, and Gate 0
  was never separately confirmed on this board.
- No `BUSY timeout!` and no `command REJECTED after ...` — the reset and
  calibrate delay deletions (C7/C8).
- No `RXFREQ_NO_FRONT_END_CALIB` in any `chip ... error:` block, and RX working
  at the operating frequency — C5. `get_errors` bit 9 clear.
- `ERR_EVENT_CAD_TIMEOUT` should stop appearing and TX should no longer stall
  in 4 s multiples — C2, the main availability win.
- `RX header latched NNNN ms with no packet, releasing TX gate` — C2's payload
  deadline. Rare is healthy; frequent means something else is wrong.
- `modem_cfg: set_tx_params(44 half-dBm = 22.0 dBm, ramp=0x05)=0` at max power
  — C1.
- `get stats`: RX count should no longer stall after a burst of TX — C4.

### CAD_LBT

```
CAD_LBT: chip took CAD->TX itself (tx_timeout=NN ms)
```

on every transmit that found the channel clear. Then confirm a neighbour
actually receives the packets — this is the whole test.

**Judge on latency only.** Post-CAD collisions are not separable from any other
loss with CoreScope, so make no collision-rate claim in either direction.

Since the CAD → Tx transition is now chip-internal, the saving *is* the
host round-trip that used to sit there. To put a number on it, flash a
build carrying only the instrumentation (`bench2a_cad_latency_instrument.patch`
on Batch 1) and record `CAD->TX latency: NNNN us` over a few dozen transmits —
median and spread, it is scheduler latency and inherently jittery. On the
combined build that line now appears only on the >524 ms fallback path.

**Expect a small number.** Before C5 this gap contained a front-end calibration
that should not have been there; measuring against that would have flattered the
result. If it is large, suspect the measurement before believing it.

### Fast CAD

`get cad`, per-level line, before and after over comparable windows:

```
*+1(22) 22p 18b 16f 2t 72%
 |  |    |   |   |  |  └─ false-positive rate
 |  |    |   |   |  └──── true positives
 |  |    |   |   └─────── false positives
 |  |    |   └─────────── busy
 |  |    └─────────────── probes
 |  └──────────────────── level (peak)
 └─────────────────────── current operating level
```

A materially *lower* busy rate is a **red flag, not a win** — it more likely
means CAD stopped detecting than that the channel got quieter. What you want is
busy and false-positive rates roughly unchanged, with CAD completing sooner.

Side detectors are already off during CAD, so the datasheet's multi-SF caveat
("the main SF determines the not-detect condition") does not apply.

Note: stacked with CAD_LBT, a change in these counters is not attributable to
either mechanism alone. If a number looks wrong, split them before chasing it.

## The 524 ms ceiling

`cad_timeout` is 24 bits of 32 MHz periods — DS §6.3.11, `ds.txt:9435`:
"expressed in periods of 32MHz crystal oscillator". **Not** the "PLL step of
31.25us" the vendor header's doc comment claims, and not RTC steps. So
`0x00FFFFFF / 32e6` = **524 ms** of Tx timeout, against the 5000 ms the classic
path uses.

The code computes `airtime + 25 % + 50 ms` and takes the classic CAD → host →
`SetTx` path when that exceeds the ceiling, rather than letting the chip cut the
transmission off mid-packet. At SF7/BW250 a 100-byte frame is ~80 ms and fits
easily; SF10/BW250 at the same length is ~720 ms and takes the fallback.

**On a slow preset you may see no `CAD_LBT:` lines at all.** Check for

```
CAD_LBT: airtime NNN ms needs NNN ms Tx timeout, over the 524 ms cad_timeout ceiling — using host CAD->TX
```

at DBG before concluding the experiment failed.

## If it fails

Nobody has hardware CAD_LBT working on an LR2021. USP #125 reported the
`RAL_LORA_CAD_LBT` path non-functional; that entry is absent from the current
`KNOWN_LIMITATIONS.md`, which is the removal of a warning, not a statement that
it works. RadioLib never implemented LoRa CAD on this chip at all — its
`scanChannel()` uses the generic RSSI CAD. **Failure is an acceptable outcome.**

| Symptom | Meaning | Action |
|---|---|---|
| No `CAD_LBT:` line, no TX at all | Chip rejected `SetLoraCadParams` with exit mode 0x10. Look for `command REJECTED after configure_cad_params`. | Revert the CAD patch |
| `CAD_LBT: no CAD_DONE within budget` | Chip does not raise CAD_DONE in this exit mode. The code aborts, re-arms RX and returns `-EIO`; the mesh retries. | Revert the CAD patch |
| Neighbours receive truncated packets | The `cad_timeout` unit is wrong after all | Revert immediately |

`build_b1_x1/zephyr/zephyr.uf2` is Batch 1 without any CAD change — the fallback
image to drop back to.

## Individual patches (superseded by `cad_lbt_fast.patch`)

Kept because each isolates one delta, and because bench 2a alone is how you get
the latency baseline. `bench1` and `bench2b` conflict textually — both edit the
same `cad` struct initialiser — so apply 2a, then 2b, then bench1 by hand, or
just use the combined patch.

| Patch | Applies on top of |
|---|---|
| `bench1_pnr_delta_8.patch` | Batch 1 |
| `bench2a_cad_latency_instrument.patch` | Batch 1 |
| `bench2b_cad_lbt.patch` | Batch 1 + 2a |
