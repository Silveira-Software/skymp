# R38 — native collision, line of sight and server-authoritative impact

Branch `codex/r38-native-validation-20261029`, base commit f50f873. PUBLIC remains
R35/R3. Nothing was installed, restarted or deployed; `productionWritten` and
`realSkyrim` are both false for every build recorded here. The managed hit
protections stay in place because the native impact path is still not proven
authoritative.

`SrpCollision` (server-only scene, immutable bindings), `SrpCollisionLoader`
(startup provenance from actual bytes) and `SrpOriginalImpact` (outbox/intent,
single hit, replay receipts) are new. `WorldState`, `ServerState` and
`ScampServer` gained the install, session-incarnation and N-API bindings.

## Focused validation

`ctest`-independent run of `unit` with `[srp-collision],[srp-collision-loader],[srp-original-impact]`:

| Attempt | Cases | Assertions | Result |
| --- | --- | --- | --- |
| r38-collision-build-20261031 | 14 | 33/42 | FAIL, 9 cases |
| r38-collision-build-20261101 (first) | 14 | 79/80 | FAIL, 1 case |
| r38-collision-build-20261101 (final) | 14 | 96/96 | **PASS** |
| r38-collision-build-20261101 (dynamics gate) | 15 | 105/105 | **PASS** |
| r38-collision-build-20261101 (attacker proxy grip) | 16 | 111/111 | **PASS** |
| r38-collision-build-20261101 (real scene through the loader) | 17 | 121/121 | **PASS** |

Final evidence: `/opt/skymp/candidates/r38-collision-impact-20261101/build-1793551056120724002`.
Toolchain image `sha256:3bbc25fdca033c192d2dc66c661ddf998036109127c039de69c41acb0b93d9db`.
`scam_native.node` sha256 `946b8990abfedec25a35f331d4b8904bdc6a9747b892a113a4316bd3227c93a7`,
`unit` sha256 `de84070e5c48b86ad04a700b161999cea69f5a22ff08052a52bd179bc1f5ab3f`.
The C++ itself compiled without errors; every failure in the recorded attempts was
in the new test surface, not in the translation units under test.

## Defects closed

1. `Digest()` erased `checksum` from a JSON **array** (`targetMeshDigest` is built
   over an array of triangles), throwing `json.exception.type_error.307` and
   aborting every impact that reached contact proof. Now only objects are erased.
2. The collision fixture omitted `base` and `referenceTransform`, which
   `Scene::Load` requires per geometry entry, so five collision cases died in
   `json.exception.out_of_range.403`.
3. The impact frame revision was a per-capture counter, so `Begin` and `Apply`
   could never produce the same proof and every legitimate hit ended in
   `SRP_IMPACT_FRAME_CHANGED`. The snapshot revision is now the target's durable
   save generation, which `Begin` and `Apply` already require to be unchanged; a
   target save still invalidates a stale frame.
4. Impact cases share one `PartOne` and the same cow/player forms, so a scene, a
   pending operation or a rate-limit window leaked between cases and the suite
   was order-dependent. Each case now clears the scene and the impact properties
   and waits for the clear to become durable.
5. The "turned away" case asserted `SRP_IMPACT_SERVER_SEGMENT_MISSES_TARGET` but
   hit `SRP_IMPACT_PRIOR_SAVE_PENDING`, because the 180° turn is itself an
   unsaved change. The turn is now made durable before admission runs.
6. The synthetic scene pinned coverage to a fixed ±10000 world box, but the
   fixture cow lives in WhiterunWorld (form `1a26f`) at roughly x≈17k. No
   coverage region matched the query, so line of sight could never be proven
   clear (`SRP_IMPACT_SERVER_COLLISION_NOT_CLEAR`). Coverage is now anchored on
   the target's real position.

## What these tests do prove

Physical triangle intersection in both directions, transformed vertices,
coplanar/edge hits, refusal to infer empty space from endpoints or missing
regions, complete dynamic-snapshot gating, immutable document digest and binding
provenance, nonfinite/zero-length query rejection, loader rejection of relative
paths, asset-root escape, wrong load order and modified bytes, and for impact:
authenticated session plus host, native melee reach, server-only segment and
contact, one hit per operation, idempotent `requestId` replay, reconciliation
from the target receipt, and rate limiting.

## Remaining blocks

- A real scene producer now exists (`tools/srp-collision-scene`), built on the
  shipped BSA/NIF data rather than fixtures, and it produced WhiterunBreezehome
  (139 imported references, 9014 triangles, 75 assets). That scene now loads
  through `SrpCollision::LoadServerScene` on the server with full provenance
  validation, and a ray against one of its real triangles comes back `Blocked`
  while a ray outside its audited coverage comes back `Unknown`. The pin is the
  `Scene::Digest` of the document as nlohmann re-serialises it, computed on the
  native side. One cell is not world coverage.
- Asset paths are portable: the producer emits forward slashes and the loader
  normalises backslashes, because a scene produced on Windows is consumed on
  Linux where a backslash is a filename character.
- `WorldState` no longer claims `dynamics.complete = true` on its own. A dynamic
  snapshot is only complete when the installed `dynamicAudit` matches the cell,
  declares completeness and lists its blockers, and none of those blockers is
  live; otherwise the impact is refused (`SRP_IMPACT_SERVER_FRAME_UNAVAILABLE`).
  Door swing and movable-body geometry are still not modelled, so a cell with
  live doors refuses melee rather than guessing, and other actors are not
  treated as blockers.
- The contact origin is no longer a fixed 64-unit offset: it is read from the
  attacker's own collision proxy (55% of its height, bounds-checked and refused
  when the proxy is missing, degenerate or desynced from the accepted position).
  The segment still runs horizontally along the accepted facing, so a target far
  above or below the grip is not reachable; `enginePoseParity` stays false and no
  animation packet proves a swing.
- Lethal damage is refused (`SRP_IMPACT_LETHAL_INTEGRATION_PENDING`); death,
  loot and XP are untouched, and the managed protections remain.
- Only the three focused tags were run. The full unit suite was not re-run, and
  PUBLIC 7777, Mongo, CDN and the launcher were not modified.
