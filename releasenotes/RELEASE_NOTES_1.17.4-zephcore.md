# ZephCore 1.17.4-zephcore

Storage housekeeping, plus a listen-before-talk fix. The repeater's `erase` never actually erased,
a node flashed from another firmware could start out with somebody else's leftovers underneath it,
and switching a node between companion and repeater firmware quietly let the two share the same
128 KB. Separately, the channel-activity detector was using the wrong reference table on LR1110
boards, and the companion's built-in `v` contact carried an unusable key on about half of all nodes.

> [!IMPORTANT]
> **Read the role-switching section before you flash a different role onto an existing node.** A
> companion that gets repeater firmware — or the reverse — now erases itself on first boot. That is
> intentional, but it is new. Export your identity first if you want to keep it.

> [!NOTE]
> A normal upgrade is unaffected. Repeater to repeater, or companion to companion, keeps your
> identity, settings, contacts and phone pairing exactly as before.

---

## `erase` now erases

On a repeater or room server, `erase` promised to format the entire filesystem. It deleted the files
in its own folder and cleared the phone pairings, and left everything else where it was.

That only mattered when a node was unhealthy — which is precisely when somebody reaches for `erase`.
If anything had written into the storage area from outside, deleting our own files could not undo it,
and the command reported success while the problem stayed.

`erase` now wipes the storage area, the phone-pairing store, and external flash where a board has it,
then reboots. The node comes back exactly like one out of the box.

> [!IMPORTANT]
> **This is a genuine factory reset now, and it takes the identity with it.** Anyone who had your node
> in their contacts will need to add it again, and the admin password, ACL and region map go too.

The companion's `erase` already worked this way. Repeater, room server and observer now share its
implementation, so there is one behaviour to remember instead of two.

---

## Switching a node between roles now wipes it

Companion and repeater firmware keep their files in separate folders, and until now each left the
other's alone. Flashing back and forth preserved both sets.

That sounds generous and was not. The two roles share a single 128 KB storage area, so a companion's
few hundred contacts and full advert cache leave noticeably less room for a repeater's region map and
access list — and a write that no longer fits simply fails. Nothing is corrupted; the node just runs
out of space for reasons its owner cannot see, because half of what is stored belongs to firmware that
is not running.

From 1.17.4 each role checks on first boot whether the storage belongs to it, and formats everything
if not.

> [!IMPORTANT]
> **Export your identity before switching roles.** Identity, settings, contacts, channels, access list,
> region map and phone pairings all go. There is no undo and no prompt — the first boot on the new
> firmware has already done it by the time you see anything.

> [!NOTE]
> **Repeater, room server and observer still share.** Those three keep their files in the same place
> and use the same settings layout, so moving between them keeps identity and configuration. It is the
> companion that is now separate.

---

## A node coming from other firmware starts clean

Nothing about installing firmware erases storage. Dragging a UF2 writes the program and nothing else,
and each firmware only clears the piece of flash it believes is its own. On the nRF52840 boards,
Arduino MeshCore's storage sits inside the region ZephCore uses, so whichever boots first tidies its
own corner and leaves the rest. On one Seeed Solar Node that produced a repeater which came up looking
perfectly healthy and silently refused to forward anything, because a single setting deep inside its
configuration had been overwritten by another firmware's bytes.

Each role now checks on first boot whether the storage is its own and, if not, clears the whole lot
before writing anything.

> [!IMPORTANT]
> **Moving between Arduino MeshCore and ZephCore still needs an erase in both directions.** ZephCore
> cleans up on the way in but cannot clean up on the way out. Run the formatter UF2, or a full chip
> erase, when you switch either way.

---

## Listen-before-talk was too cautious on LR1110 boards

Before transmitting, a node listens for a LoRa signal already on the air. How faint a signal counts is
set by a per-chip threshold, and ZephCore tunes it automatically from what each node measures.

The starting values for that tuning came from a reference table — and on the LR1110 it was the wrong
table, copied from a different Semtech chip and read at the wrong setting. It started roughly five
steps too insensitive, so those nodes spent weeks walking the threshold down and still hit the limit of
how far they were allowed to adjust. Two nodes sitting in one room made it visible: an SX1262 settled
one step from its starting point while the LR1110s next to it were pinned at the end of their range.

The tables now come from Semtech's own reference code, and they take **bandwidth** into account, which
nothing did before. That barely moves the SX1262 — a count or two, and nothing at all at the default
preset — but on the LR1110 bandwidth is worth around twelve counts per doubling, which is most of the
error. The range each node may adjust within is wider, and the radio now tells the tuner where its own
limits are, so a node can no longer sit against a wall it cannot see.

Affected boards: **T1000-E**, **ThinkNode M3** and **ThinkNode M9**. On SX1262 boards nothing changes
unless you run a 250 or 500 kHz bandwidth, where the old value was up to ten counts too sensitive.

> [!NOTE]
> **Run `set cad.reset` after upgrading.** The tuning statistics your node collected are measured
> against the old starting point and are not comparable to the new one. Clearing them lets the tuner
> re-converge cleanly; left alone it blends two sets of readings. Everything else is automatic.

> [!IMPORTANT]
> This is a first release of the corrected tables. They are verified against Semtech's reference and
> against on-air measurements from three nodes, but not yet across a season or a busy site. If a node
> starts deferring noticeably more or less than it used to, `get cad.stats` shows what it is measuring.

---

## The built-in `v` contact had an unusable key on half of all nodes

Every companion offers a contact named after itself with a `v` in front — the loopback chat that runs
the console commands. Its key was built in a way that produced something key-shaped but, on roughly
half of all nodes, not a valid key for the curve the protocol uses.

The official app never checked and so never minded. Other clients do check, and refused either to add
the contact or to send it a message. That is why the v-contact has worked for some people and not for
others with no apparent pattern: it was a coin flip settled when the node's identity was created, and
nothing the owner did afterwards could change the outcome.

The key is now derived properly and is valid on every node. Nothing else about the v-contact moves —
it is still local to the app, still never touches the radio, and still has no private key stored
anywhere.

> [!IMPORTANT]
> **The v-contact's key changes with this release, so your app will keep showing the old one.** Delete
> the leftover `v<name>` entry by hand. The new one arrives on its own the next time the app connects.

---

## Also in this release

Nothing here changes how a node behaves.

- **A wasted erase on the companion.** Running `erase` from the companion's USB console formatted the
  storage, then formatted it again on the reboot that followed, because the marker saying "this node
  has been set up" went out with everything else. Both paths behave the same way now.
