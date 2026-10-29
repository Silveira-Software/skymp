# R38.2 — original corpse quarantine and atomic race/resource writes

Offline candidate only. PUBLIC R35/R3 was not changed. Full High Fantasy,
supernatural engine transitions and authoritative SPEL remain RELEASE_BLOCKED.

`SrpOriginalLifecycle` extends the optional R38.1 original ACHR identity. Native
Kill materializes the existing death-item selection and records one
`private.srpOriginalLifecycleV1` receipt in the same ChangeForm transaction.
The operation is `<instanceId>:death:0`, state DEAD_QUARANTINED. A repeated
kill does not select more drops or call the legacy reward callback. This is a
native death observation, **not** independent hit/collision or reward authority.
Loot, XP and respawn remain disabled for adopted originals. An orphan death
receipt without the identity marker also stays quarantined: inspect, kill,
unload and fresh adoption fail closed. A contradictory alive bit cannot
mint a replacement identity. Engine death messages can precede fsync and are
not a durable reward ACK. Legacy actors
without the marker retain their existing paths.

Original TakeItem/PutItem/Activate and unapproved resurrection/25-second
respawn paths are blocked. Unload requires a saved generation and retains the
current ChangeForm in the existing deferred-load cache, clearing the old
transport host lease. `inspectSrpOriginalActor` is a server-only no-load reader;
its host field explicitly does not prove damage or collision authority.

Managed SetAppearance now writes appearance and resource reconciliation in one
existing ChangeForm transaction. The added invalid-race test failed against
the previous method: an exception left the invalid appearance queued with old
maximum metadata. It now preserves the entire prior record and generation.
The R38.1 absolute-resource behavior and `srpResourceMaximumsV1` remain intact.
This does not implement vanilla transformation, animation, controls or voice.

The full test run also exposed a detached watchdog in LeveledListUtilsTest:
its stack atomic could die before the thread, causing an unrelated test to
abort ten seconds later. A GDB trace located that thread. It is now joined;
the original workload, ten-second timeout and expectations are preserved.

Validation on 29 October 2026:

- clang-20 / CMake / Ninja, existing `srp-native-v129-openmp:20261011` toolchain.
- 355/355 full C++ cases; repeated with previously failing seed 3286815900,
  again 355/355. Assertions vary with randomized leveled-list selections.
- Nine original identity/lifecycle cases; 168 regression cases are a subset.
- 562 source/header files match the actual isolated build tree (normalized LF).
- Actual addon + original HF ESP: five identity-recovery and eight corpse
  recovery cases, including SIGKILL, lost ACK, unload/reload and corruption.
- Real addon resource tests, full gamemode integration, isolated Mongo,
  allocation/outbox crash recovery and legacy readers passed in PR64 evidence.
- Skyrim human validation and transformation/voice/impact remain NOT TESTED.

Addon SHA-256: `cef5b2687f7461fb212dfcc3135fbf784dc35493a86e8603c849541f69b7e31d`.
Unit SHA-256: `2d2d63c4e4e201dcc459793a921c99ce37f300b8bd439ddde8a739e65abb534e`.
Full reports, source manifest and controlled acceptance package: gamemode
[PR64](https://github.com/Silveira-Software/SKYRIMRP/pull/64), `docs/r38-2`.

Rollback must keep the latest world, markers, receipts and this compatible
native behavior. An R38.1 identity-only reader cannot safely operate a corpse
receipt. Disable the optional observer without deleting data; do not restore
old world snapshots over later progress. No production migration was run.
