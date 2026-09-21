# ZephCore 1.17.5c-zephcore

A fix release on top of 1.17.5b. The main one: messages and `v` contact replies could stall and only
turn up on the next reconnect. Alongside it, companions now originate 2-byte path hashes like
repeaters always have, and a repeater with a bad stored return path re-learns it instead of
answering into the void.

> [!NOTE]
> A normal upgrade keeps your identity, settings, contacts and phone pairing. Nothing needs redoing.

---

## Messages and `v` contact replies arrived only on reconnect

Ask the `v` contact something, watch the message go out delivered, and get nothing back — then
reconnect and the whole backlog appears at once. Two separate causes, both fixed.

The app re-issues its session-start command whenever the device-settings screen opens, not only on
connect. That set a latch meant to stop message prompts from interrupting an initial contact sync,
and the only thing that cleared it was a message sync running to completion — which a
mid-session app is under no obligation to do. On a connection hours old, every reply, battery alert
and notice after that point queued silently. The latch is now bounded, and the thing it actually
guards (a prompt landing in the middle of a contact dump) is checked directly instead of inferred,
so the bound can be short.

Second, a queued message reached the app through exactly one best-effort prompt and nothing ever
retried it. If it was dropped by BLE congestion, suppressed by the latch above, or ignored because
the app thought a sync was already running, the queue sat there until the next connect. There is now
a watchdog that re-prompts once the app has gone quiet — the same treatment the contact dump and BLE
advertising already had. Duplicate prompts are harmless, so re-asking costs nothing.

---

## Companions were originating 1-byte path hashes

Path hash size is a per-node setting: repeaters and room servers have defaulted to 2-byte hashes
since ZephCore added the option, which collide far less often on a dense mesh. That default lived in
the repeater and room-server startup code only, so a fresh companion quietly originated 1-byte
hashes and reported the wrong mode to the app. It is now a shared default for every role.

Three related bugs went with it:

- `set path.hash.mode default` wrote 0 (1-byte), not the real default — including on a repeater that
  had booted at 2-byte.
- A corrupted settings byte fell back to 0 rather than the default.
- A directly-heard advert carrying a 2-byte hash had its hash size stripped on the way in and was
  read back as 1-byte by everything downstream.

> [!NOTE]
> **Existing nodes keep whatever they have stored.** This changes what a factory-fresh node starts
> with, and what `default` means. `get path.hash.mode` shows the current value.

---

## A repeater could answer into the void

The client list a repeater or room server keeps on flash stores each client's return path. Nothing
validated that byte on load, and one malformed value passed the "do we have a path?" test but was
then rejected deeper in, so the reply went out as a zero-hop direct — reaching only immediate
neighbours, never retried, with the repeater considering the request answered. Such a path is now
discarded on load and re-learnt, costing one flooded reply.

---

## Remote `get gps diag`, `cad.stats` and `meshtimesync` could overrun their buffer

These three replies size themselves to fit the packet buffer. When the request carried a tag prefix,
the repeater echoed three bytes into the front of that buffer and the handlers did not know, so they
could write past the end of it. They are now told how much room is left.

---

## Also in this release

- **The companion's recently-heard-node table could evict itself.** It wrote round-robin with no
  lookup, so a node heard repeatedly filled several slots and pushed other nodes out, and the app
  could then be handed a stale route for a node whose path had changed. It now updates a node's own
  slot and replaces the least recently heard.
- **Platform: Zephyr moved to `7d713288b4b`** (main, `v4.4.0-16108`). Fifty-six upstream commits, no
  patch rebases, no module pins moved, no ZephCore changes required.
- **CI builds on a pinned runner image** (`ubuntu-24.04`) with third-party actions pinned to commit
  SHAs, so the `ubuntu-latest` flip to Ubuntu 26 in October does not silently move the toolchain
  under a release build.
