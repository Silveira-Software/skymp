# R38.1 native — offline, RELEASE_BLOCKED

No production installation or Windows Platform hook. Public R35/R3 preserved.

Implemented:
- Server-only adoptSrpOriginalActor for an already loaded original ACHR. Canonical plugin/local IDs, base/location checks, stable native instance, optional durable marker, generation-confirmed save. No spawn, teleport, new reward life or host grant.
- LoadForm returns an existing reference before initialization. AddForm(skipChecks) preserves an existing object. Before the fix, chunk discovery could initialize a second object, queue its save under a second idx, discard the object, then overwrite the real actor's world file with the orphan snapshot. Both C++ fsync and real-HF addon tests reproduced the missing receipt.
- Managed SetAppearance reconciles resource maxima and absolute resources with the existing engine, without an independent buff/stat subsystem. This is a dependency correction, NOT a full supernatural transformation protocol.
- CTDA GetLevel reads validated canonical SRP player progression, native profile/current character and a positive revision not behind native projection. NPC auto-calc is UNKNOWN. Allows canonical-leading projection update without a composition deadlock.

Validation:
- Official isolated clang-20/CMake/Ninja image srp-native-v129-openmp:20261011; network none.
- 349 C++ cases PASS / 511449 assertions (counts can vary with generated fixtures/order).
- 559 server/library/unit source/header hashes match the build tree.
- Exact addon + real current 48-plugin load order + local original HF ESP: five restart/crash/corruption scenarios PASS. Confirmed identity survives ACK loss; before durability, retry can mint an unconfirmed identity without rewards/items.
- Addon resource/equipment/relog tests; full candidate gamemode; isolated Mongo allocation with nine SIGKILL windows; outbox/legacy receipt replay; reader cutover and native regressions PASS.

Native SHA-256: `987dcaa4e5b91ba5f420647dc9ad9a7dfc2e9602341e8d231fc4042237e4c764`.
Unit SHA-256: `6ca553a0be770e88a16bafcef8d2ab9626116f6fb68cb6ccb4ac137cd1c22f72`.
Evidence: D:/srp-work/r38-1-operation-20261029; detailed public PR64 docs/r38-1 evidence. No third-party game assets included.

Still blocked: pre-engine AE1170 hook/ABI/cancel/concentration/dual acceptance, server final cost and independent impact/LOS, NPC lifecycle reward/respawn/host epoch, full form transformation/equipment/control/voice recovery and real two-player Skyrim acceptance. Spell/points/HF/forms OFF.

Compatibility: optional private.srpOriginalReferenceV1 marker. Preserve all old effects, receipts/outbox and srpResourceMaximumsV1. Never erase confirmed identities or restore an old world/inventory over new progress. Rollback of active HF needs the LoadForm correction and a compatible original-reference reader; old native reintroduces the identified persistence defect. This PR does not authorize public deployment.
