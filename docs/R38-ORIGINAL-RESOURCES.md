# Original NPC resource composition — candidate only

The native reads ACBS/CNAM from the winning NPC record and resolves UseStats
and UseTraits through the existing template chain. NPC_/GMST enumeration now
supports the existing CombineBrowser winning-record interface.

`applySrpOriginalStats(refId)` accepts a loaded original reference only. It
accepts no supplied level, resource amounts, class, or encounter context. It
requires a saved, alive original identity. Fixed-level resource growth uses
the original CLAS weights, fNPCHealthLevelBonus and the integer GMST
iAVDhmsLevelUp. Fractional distribution follows the captured AE routine:
floor(budget/sum)*weight, then one remaining point per positive weight in
descending order, with later entries first on ties. Zero weights skip growth.
Race bases and health-level bonus are truncated separately into signed 16-bit
caches. Overflow is refused instead of reproducing wrapped negative resources.

The pure base-NPC PC multiplier resolver reproduces AE float multiplication,
truncation and ACBS min/max. That captured method reads the global player's NPC
level, not an ECZN runtime level; its vectors do not prove final per-actor
encounter resolution. It is not an encounter-level producer: Preview still refuses
PC-level scaling until a persistent, canonical CELL/EZ context exists. There is
no client/host-supplied context and no new RPC that can grant an arbitrary level.

The method preserves absolute Health/Magicka/Stamina in the same ChangeForm
transaction as the snapshot. A reduction that would clip current resources
is refused. Repeat calls compare the source snapshot and do not add growth.
The snapshot has a canonical SHA-256 checksum; the existing
`private.srpResourceMaximumsV1` format remains version 1. Reads restore those
same maxima and fractions after restart. No player progression is enabled.

Baseline validation for 433988f on 30 October 2026: 62 focused C++ cases; 393/393 full native cases;
actual addon resource/write/restart tests for five fixed-level High Fantasy
bases; expected refusal for original corpses, protected base, and PC-level
context; actual SIGKILL after save before ACK recovered identical state.
Evidence: `D:/srp-work/r38-native-gameplay-20261030/`.
Candidate addon SHA-256:
`f0c6227374ccc3ac898f2e68a5384176c7f2365bc760878413a4eeddbaf9cbf7`.

This is not a gameplay release. Engine parity remains HUMAN_PENDING. Skills,
the durable PC-level/encounter producer, authoritative impact, original
death rewards and CELL/EZ reset are incomplete. OnHit/OnWeaponHit Managed
guards remain active. No public actor is activated. Old native readers must
not be used on newly composed candidate records: they ignore the resource
snapshot. Keep the forward-compatible candidate reader and pending records
when rolling back gamemode; PUBLIC has no records produced by this method.

## AE evidence and compatible receipt evolution (30 October)

The local, separate vanilla AE executable was inspected read-only after Steam
initialization. SKSE and Skyrim Platform were not loaded; it did not connect to
PUBLIC. No functions were injected or invoked by the probe. It was closed after
code capture. Executable SHA-256:
`c434208894f07f604b852f29b8edc3a58c4de63de783373733e72b2b73f33be9`.
Captured functions: GetLevel RVA 1d7cf0, auto-calc initialization 3bf890,
distribution 3c5140. Distribution byte-range SHA-256:
`90ffdf94b996d3c038cc4d11965dbbf5399663142b87968fb85255c79422a779`;
GetLevel byte-range SHA-256:
`1a33c10d0ffb8055f24dc1430560bc28043e2b929a67de14eef12366fa3b6b77`.
Unicorn executes the captured instructions, producing the committed fixtures:
160 allocation vectors, 12 cache initialization vectors and 872 level vectors from all 218 resolved stats-source
records in the existing catalog context (including rejected zero-level corpses).
This is instruction emulation, not observation of spawned creatures in Skyrim.
The 113 references whose template requires a non-NPC selection remain unresolved.
Private captures/reproducer: `D:/srp-work/r38-encounter-impact-20261030/`.
Portable reproducer: `misc/srp-original-stats-engine-oracle.py` (Python +
Unicorn; pass --text, --catalog, --header and --report). It verifies the three
captured code ranges before executing. No Bethesda code is distributed here.

New V1 snapshots add resourceLevelUp and resourceLevelUpSource; existing V1
readers ignore these additive fields and retain the frozen resource maxima.
A retry with an old V1 receipt must validate its checksum and identity first.
Only a ten-point current GMST and otherwise identical complete snapshot permit
an equivalent retry; the old receipt is returned unchanged, without a save or
recalculation. A changed maximum/source/identity requires explicit migration.
Partially annotated or corrupt receipts are not normalized. No production
migration is performed. Gameplay and rewards remain disabled.

Latest verified candidate (30 October): 67/67 focused C++ cases, 398/398 full
native cases, zero failures. Official toolchain image remains
`sha256:3bbc25fdca033c192d2dc66c661ddf998036109127c039de69c41acb0b93d9db`.
All 685 selected tracked native/server/unit/CMake source inputs match the branch
(CRLF/LF normalization only); generated fixture header is included in that check.
Addon resource/write/restart tests cover all 31 distinct fixed-level stats-source
representatives selected from the existing context: 24 succeeded twice, five
were refused by original protected/unique flags, two by the winning native
CrimeFactionsList. Policy refusals are not functional creatures or resource PASS.
The initial expanded probe stopped at the first crime-faction refusal; the
completed probe verifies that actual FLST/SNAM policy and records it as BLOCKED.
No native admission guard or test expectation for resource correctness was removed.
Five prior-addon V1 storage copies reload/retry unchanged. Actual SIGKILL after
stats save before ACK recovers identical maxima, fractions and receipt. None of
these tests prove engine combat, actor encounter resolution, loot, XP or reset.
New addon SHA-256:
`073c3912b2d37b59fd41a18e40eb9de8c1b6d6818a7202cd66af1124f711a780`.
Unit executable SHA-256:
`2309469b4a307a91a3e48efe3bb865dd71fa6421f1a72a983501a228cbc668d6`.

Next blockers: Preview passes no PC context to ResolveLevel, so scaled actors
still refuse SRP_ORIGINAL_STATS_ENCOUNTER_LEVEL_REQUIRED. ActionListener::OnHit
and OnWeaponHit still return for SrpOriginalLifecycle::Managed: no trusted
server collision/LOS scene or durable unique-impact producer exists here.
Original death rewards and CELL/EZ/quest reset remain gated behind that proof.
Zero original High Fantasy references have a newly approved full gameplay cycle.
PUBLIC, Mongo, CDN and client installations have not been changed by this work.
