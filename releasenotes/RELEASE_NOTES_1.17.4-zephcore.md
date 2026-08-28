# ZephCore 1.17.4-zephcore

Storage housekeeping. The repeater's `erase` command never actually erased anything, a node flashed
from another firmware could start out with somebody else's leftovers underneath it, and switching a
node between companion and repeater firmware quietly let the two share the same 128 KB. All three are
fixed, and the last one is now deliberate and loud rather than quiet.

> [!IMPORTANT]
> **Read the role-switching section before you flash a different role onto an existing node.** A
> companion that gets repeater firmware — or the reverse — now erases itself on first boot. That is
> intentional, but it is new. Export your identity first if you want to keep it.

> [!NOTE]
> A normal upgrade is unaffected. Repeater to repeater, or companion to companion, keeps your
> identity, settings, contacts and phone pairing exactly as before.

---

## `erase` now erases

On a repeater or room server, `erase` promised to format the entire filesystem. It did not. It deleted
the handful of files it had put in its own folder and cleared the phone pairings, and left everything
else exactly where it was.

Most of the time nobody noticed, because on a healthy node those files *are* everything that matters.
It mattered when the node was not healthy — which is precisely when somebody reaches for `erase`. If
anything had written into the storage area from outside, deleting our own files could not undo it, and
the command reported success while the problem stayed.

`erase` now wipes the storage area itself, the phone-pairing store, and external flash where a board has
it, then reboots. The node comes back with a new identity and default settings, exactly like a node out
of the box.

> [!IMPORTANT]
> **This is a genuine factory reset now, and it takes the identity with it.** Anyone who had your node
> in their contacts will need to add it again, and an admin password, ACL and region map all go too.
> That was always what the command claimed to do; it is now what it does.

The companion's `erase` already worked this way. Repeater, room server and observer share one
implementation with it now, so there is one behaviour to remember instead of two.

---

## Switching a node between roles now wipes it

Companion firmware and repeater firmware kept their files in separate folders, and until now each left
the other's alone. Flashing back and forth preserved both sets.

That sounds generous and was not. The two roles share a single 128 KB storage area. A companion that has
collected a few hundred contacts and a full advert cache leaves noticeably less room for a repeater's
region map and access list, and a write that no longer fits simply fails. The node is not corrupted —
the two roles' files never sit on top of each other — it just runs out of space for reasons its owner
cannot see, because half of what is stored belongs to firmware that is not running.

They are also two quite different kinds of node, and treating one machine as quietly holding both was
never worth the space it cost.

From 1.17.4, each role checks on first boot whether the storage belongs to it, and formats everything if
not. So a repeater flashed onto a former companion starts empty, and a companion flashed onto a former
repeater starts empty.

> [!IMPORTANT]
> **Export your identity before switching roles.** The node's identity, settings, contacts, channels,
> access list, region map and phone pairings all go. There is no undo and no warning prompt — the first
> boot on the new firmware has already done it by the time you see anything.

> [!NOTE]
> **Repeater, room server and observer still share.** Those three keep their files in the same place and
> use the same settings layout, so moving between them keeps the node's identity and configuration. It
> is the companion that is now separate.

---

## A node coming from other firmware starts clean

Flashing ZephCore onto hardware that was running something else is a normal thing to do, and it used to
leave more behind than anyone expected.

Nothing about installing firmware erases storage. Dragging a UF2 file writes the program and nothing
else, and each firmware only ever clears the piece of flash it believes is its own. On the nRF52840
boards, Arduino MeshCore's storage sits inside the same region ZephCore uses, so the two overlap — and
whichever one boots first tidies up its own corner and leaves the rest of the other's files sitting
there. The result on one Seeed Solar Node was a repeater that came up looking perfectly healthy and
silently refused to forward anything, because a single setting deep inside its configuration had been
overwritten by bytes that belonged to a different firmware.

Each role now checks on first boot whether the storage is its own and, if not, clears the whole lot —
the storage area, the phone-pairing store, and external flash — before writing anything. A node arriving
from another firmware, or from a factory-fresh chip, starts from a known state instead of an inherited
one.

> [!IMPORTANT]
> **Moving between Arduino MeshCore and ZephCore still needs an erase in both directions.** ZephCore now
> cleans up on the way in, but it cannot clean up on the way out — going back to Arduino MeshCore leaves
> ZephCore's files inside the area Arduino will use. Run the formatter UF2, or a full chip erase, when
> you switch either way. This is not new advice; it is now written down.

---

## Also in this release

Nothing here changes how a node behaves.

- **A wasted erase on the companion.** Running `erase` from the companion's USB console formatted the
  storage, then formatted it a second time on the reboot that followed, because the marker saying "this
  node has been set up" went out with everything else. The pairing-based factory reset never had this
  problem. Both paths behave the same way now.
