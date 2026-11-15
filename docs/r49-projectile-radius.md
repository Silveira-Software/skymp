# R49 — original projectile volume (14 November 2026)

Status: IMPLEMENTATION_UNDER_TEST. Isolated LAB only; no PUBLIC deployment.

Preflight, Obsidian protocol/index/state/map/system/decisions/regressions/matrix,
Native PR7/825ddc9 and Game PR66/ea50db6 were consulted. Existing untracked
AGENTS/scripts/cache files are preserved.

## Comparison and evidence

The R8 human session connected, transformed into Lord, equipped and cast original
33657636. Admission refused SERVER_SEGMENT_MISSES_TARGET / PRIOR_SAVE_PENDING;
the first samples had no new outbox or target receipt, HP100/Magicka85 and target95/145.
The human saw red magic, local damage, then target health restoration. Returned
human with equipment and closed both applications. Later inspection found three
subsequent R8 APPLIED receipts at 01:24:49–55UTC: each15damage/15healing/10cost,
target95→80→65→50, player Magicka85→55. Each outbox healingApplied15 matches
the corresponding target receipt. R8 therefore has real damage/heal/cost proof;
this radius change addresses the remaining refusals. Return is APPLIED/human,
but fence remains true after early exit; release recovery is still pending.
See Game evidence/r49/human-r8-lord-impact.json for curated real receipts.

R8 fixes frame changes after durable admission; these new refusals happen before
admission. Contact currently treats all attacks as a zero-width horizontal ray.
The installed Dawnguard PROJ 1a30c (DLC1VampireAbsorbProjectile) DATA is 92 bytes:
range10000, missile, collision radius24 at file offset72. Mutagen's Skyrim
Projectile.xml independently specifies this layout:
https://raw.githubusercontent.com/Mutagen-Modding/Mutagen/master/Mutagen.Bethesda.Skyrim/Records/Major%20Records/Projectile.xml
This is file layout, not the pointer-aligned runtime BGSProjectileData layout.

A diagnostic using saved poses sampled after the close-range refusal, and the
actual pinned controller meshes, finds ray-to-target distance21.3546532 at
115.827962 units along the ray. Zero-width misses; radius24 touches. This is a
post-attempt diagnostic, not an impact receipt or exact cast-frame capture.
It proves the existing policy omits projectile volume; it does not prove every
refusal has this cause. Pitch/third-person camera parity remains a separate limit.

## Plan

Reuse SrpOriginalImpact and SrpCollision, the existing source-record reader,
outbox/target receipts, current-frame R8 checks and dynamic coverage producer.
Files: SrpCollision.h/.cpp, SrpOriginalImpact.cpp, their existing unit tests,
this note/result, and the existing isolated Game build/packaging scripts.

Read and validate the original PROJ radius server-side; bind projectile identity
and radius into the durable drain weapon. Sweep this sphere against actual target
triangles. Sweep the same radius against static/dynamic blockers and unresolved
regions, and require audited coverage of the entire volume. Existing melee/claws
keep radius0 and their exact-frame policy. No packet-supplied radius, point,
damage, target bounds inflation, disabled collision guard or second combat system.

Use analytic closest segment/triangle distance, followed by bounded root search
for first sphere contact, checking the resulting contact is inside the radius.
Keep existing ray path unchanged for radius0. Extend collision coverage
conservatively (erode complete coverage, expand blockers/incomplete coverage).

Risks: tangent precision, long range, initial overlap and missed broadphase
blockers; tests cover them. Old terminal ledgers remain readable. A preexisting
pending weapon with a different policy must cancel/refund via existing rules,
never be silently rewritten. No pending operation was present in this session.

## Acceptance, tests and reversal

Reproduce zero-width miss on actual observed geometry; test sphere face/edge/
vertex/grazing/miss/overlap, static/dynamic/unknown/coverage and nonfinite inputs.
Run existing collision, original impact, claws/drain and Hit/PartOne regressions;
load the addon and run existing real trade audit against its new hash. Preserve
native R8 and S4 artifacts. Only prepare a new isolated pair after checks pass.
Human acceptance still requires APPLIED target receipt, matching HP loss,
one cost/heal, equipment return and persistence. Native/test PASS is not gameplay
PASS. Rollback is the previous pinned R8/S4 pair plus the saved LAB snapshot;
no production world is written.


Early collision-only validation PASS:115assertions in14cases, including the
pinned real controller scene and the observed post-attempt ray gap. Static,
dynamic, unresolved and incomplete/eroded coverage guards pass. This executable
links SrpCollision.cpp and its existing tests directly with Catch2; it is not
the complete addon. Full addon rebuild/regressions are still in progress,
limited to1.5CPU/2100MiB, no network. Interrupted first compile was stopped
explicitly before tests to add initial-overlap handling and a longer bounded
build deadline; object cache preserved. Scope/evidence is distinct from human
impact. Input-lock report on later R8 reconnection is under separate investigation.
