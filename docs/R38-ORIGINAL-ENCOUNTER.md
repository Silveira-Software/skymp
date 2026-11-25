# Original encounter context — isolated candidate, 31 October 2026

`captureSrpOriginalEncounter(originalId, entrantActorId)` is a server-only N-API
method. Both actors must already be loaded. The entrant must be attached to a
live native user and have a positive profile. Its canonical `srpProgression`
must validate and match profile/active character. No supplied numeric level,
zone, maxima, context, client collision or damage is accepted.

The existing original identity must be saved first. Placement is read from the
winning ACHR and its actual parent CELL; XEZN resolves ACHR, CELL, then WRLD.
ECZN minimum/maximum/flags and the winning `fLevelScalingMult` are read through
libespm. Unknown/missing/malformed inputs fail closed. The numerical ECZN rule
was recovered from the previously captured AE 1.6.1170 code at RVA 28a210;
48 instruction-emulation vectors cover below-minimum matching, scaling and caps.

Multiplayer arbitration is explicitly `SRP-FIRST-CONFIRMED-ENCOUNTER-1`: the
first valid native capture freezes a zone seed from the canonical PC. ECZN scope
is shared; without an EZ the scope is this original ACHR. NPC multiplier/min/max
resolve the final level against that frozen seed. This is server multiplayer
policy, not a claim of vanilla per-instance or visual engine parity. NeverReset
and other zone flags are retained; there is no automatic epoch/reset yet.

The immutable receipt is `private.srpOriginalEncounterV1` in the existing
ChangeForm. It binds original instance/epoch, placement, resolved stats source,
template chain, canonical PC profile/character/revision/level, GMST, zone seed,
final level, operation identity and canonical checksums. No new database exists.
The first actor is also the zone anchor. Other actors cannot copy an unconfirmed
zone receipt. The existing RequestSave/AsyncSaveStorage generation ACK confirms
durability; memory only tracks which generation to await. Cold database images
reconstruct the context after restart without materializing an anchor. Matching
copies are reusable; contradictory histories, corrupt receipts and character-
bound/player images are refused without erasing them.

`SrpOriginalStats::Preview` now reads the saved producer for PC-scaled actors,
checks the resolved stats/template configuration and persists its context
checksum with the final resource snapshot. Existing fixed-level paths are
unchanged. Existing absolute-resource preservation is retained; the producer
does not edit resources, inventory, equipment or player progression.

Validation: 16/16 focused C++ cases (7 new encounter cases and the 9 affected
stats regressions); no rerun of the complete 398 cases or the 24 fixed bases.
The actual addon used original `aa28` and `cc0:mihailhighfantasypack.esp` in
CELL `7614c:Skyrim.esm`, EZ `3ec3e:Skyrim.esm`. Four write/restart scenarios PASS:
normal replay, SIGKILL before context save, SIGKILL after context save, SIGKILL
after stats save before ACK. Canonical character replacement does not reroll
confirmed context. Shared-zone serialization and pending-save rejection PASS;
absolute H/M/S, inventories and equipment are preserved. These are two simulated
native user sessions, not Geladao/Elohim human Skyrim tests.

Rollback: additive private metadata only; `srpResourceMaximumsV1` remains V1.
The previous 658bfcc native reader was run against both new PC-scaled states:
resources and receipts survived, its old composer refused the unsupported
operation without writing. Keep a V1 original-stat reader (433988f or later),
all context receipts and pending saves. Never restore an older world over newer
operations. No production migration, publication or maintenance occurred.

Native SHA-256: `ca439a1450cd8fbe6f7b88e6107e07a4f5ff5fcf66d348321901054cd592394b`.
Evidence/reproducers: `D:/srp-work/r38-encounter-producer-20261030/`.
Gameplay remains RELEASE_BLOCKED: no server physics scene/coverage importer,
authoritative LOS/collision or durable unique-impact protocol is integrated.
Managed hit guards, original reward quarantine and reset gates remain active.
No original High Fantasy reference is newly approved for a full gameplay cycle.
