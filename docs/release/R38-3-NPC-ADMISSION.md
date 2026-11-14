# R38.3: optional native NPC admission and fail-closed loading

RELEASE_BLOCKED. PUBLIC R35/R3 unchanged. No Windows engine/human approval.

The additive server configuration `srpNpcAdmissionV1` reserves a slot in
WorldState::AddForm before Init, indices, inventory and grid activation. Without
the configuration, admission remains disabled. Configuration is accepted only
before forms load, with strict canonical descriptors resolving in the actual
load order. Caps: total <=300, fauna <=180, other <=120, and a regional cap.
These are administration maxima; 300 active AIs have NOT been approved/tested.

Original and dynamic non-player actors share the same controller. Dead/disabled
loaded NPCs conservatively retain a slot. Players are excluded. Interiors count
by CELL; exteriors by WRLD/4096-unit grid. Movement to a full region is rejected
before changing position. Destroy releases a slot only after the existing
BeforeDestroy durability guard. Clear preserves policy and cannot clear a failed
initialization latch. Rejected references may be reconsidered after a slot frees.

Controlled source plugins require an explicit original ACHR whitelist. Winning
ACHR/NPC_ provenance catches master-owned overrides and dynamic controlled bases.
Ambiguous controlled origins require review. Unadopted controlled originals are
already quarantined, and unapproved hits/casts cannot mint rewards. This is NOT
combat authority: loot, XP, respawn, complete forms, SPEL and point spending stay OFF.

`getLoadedActors(modIndex)` returns a sorted copy of actual current actor handles
without loading forms. The old getAllForms reader remains compatible but includes
deferred images/cache, and is no longer used as evidence of loaded actors.
`inspectSrpNpcAdmission()` exposes counters/limits and initializationFailed.

With admission enabled, an Init/ApplyChangeForm exception latches failure. A
partially inserted actor retains its slot, subsequent loading/no-load access and
Tick fail, generation is zero and save ACK is false. Clear cannot bypass this
latch. The latest durable images must be retained; corruption is not repaired by
deleting receipts or restoring an old world. Tests check refusal again in a new
process, with unchanged file hashes. No persistence schema migration was added.

The first full suite caught an introduced null-parent dereference in
MpActor::ApplyChangeForm for unattached actors. The parent guard was corrected;
the original ActorTest expectations were kept. Earlier compile failures and
interrupted builds are retained in the R38.3 operation evidence.

Verified build:

- Native SHA-256: `3a321369f2408a622c2a2e7f633daa36d03a386cec61d22923ac869f9e13619e`.
- Toolchain image: `sha256:3bbc25fdca033c192d2dc66c661ddf998036109127c039de69c41acb0b93d9db`.
- Full C++ suite: 365 cases, zero failures.
- Source/header parity: 565 files, no mismatch.
- Four real-addon admission groups: override whitelist with loadable controls,
  restart, post-ACK SIGKILL/reload and corrupt saved image refusal.
- Controller test of 180+120 identities does not measure 300 active AIs.
- Full integration, replay, Mongo, launcher and rollback results are consolidated
  in PR64 docs/r38-3/R38-3-TEST-MATRIX.md after their respective runners finish.

Rollback must retain both lifecycle R38.2 readers and the R38.3 admission guard
while controlled sources may load. Disable the observer without discarding the
latest world, receipts or tombstones. An old native ignoring active admission is
not an approved rollback reader. No PUBLIC installation or restart is authorized.
