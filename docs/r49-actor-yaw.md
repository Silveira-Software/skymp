# R49 actor collision yaw — plan before implementation, 15 November 2026

Scope: isolated LAB candidate only. No PUBLIC deployment. Preflight, Obsidian
index/protocol/state/map/supernatural/decisions/regressions/matrix and GitHub
PR7/PR66 consulted. Native HEAD311e69c and Game9e49b6d are available remotely;
untracked AGENTS/scripts/cache are preserved.

The human confirmed ALVO DO TESTE on the admitted reference33585739. Client
casts21:27:14.643Z and21:27:20.377Z targeted that reference with spell579007 and
camera0. The first request was refused PRIOR_SAVE_PENDING; the second produced
an exact native contact frame and SERVER_SEGMENT_MISSES_TARGET. No receipt or
resource delta was accepted. Both casts occurred before the subsequent hand
refresh question, so they are not evidence of performing that later instruction.

Offline replay of the exact second frame against the pinned original controller
finds minimum distance11.45348777 for original projectile radius10. Inverting
only the actor yaw gives3.32766031. This alone is a diagnostic, not justification
to move the ray or enlarge its radius. The native CaptureSrpActorCollisionPose
uses counterclockwise yaw. CommonLibSSE-NG's NiMatrix3::SetEulerAnglesXYZ and
its matrix/vector product establish clockwise yaw for Skyrim's column-vector
transform: a local forward positiveY maps towards positiveX at positive90deg.
The native drain facing already follows that same clockwise convention.
Primary reference:
https://github.com/CharmedBaryon/CommonLibSSE-NG/blob/b93280e832f263dbef44e44cbe2936622a02f91a/src/RE/N/NiMatrix3.cpp
(SetEulerAnglesXYZ and operator*(NiPoint3)). This is source comparison; runtime
animation/controller parity remains unclaimed. The separate producer's static
REFR Euler conversion also deserves its own audit; do not silently change scenes
or producer hashes in this patch.

Change only the yaw signs in the existing authoritative actor collision pose.
Reuse accepted position/angle, original ACHR scale, race/sex controller geometry,
scene loader, sphere sweep, dynamic blockers and durable impact outbox. No client
hit point, damage, pose authority, camera-origin offset, radius padding or new
protocol. Caster grip remains the existing explicit proxy policy, not an engine
animation pose. Existing applied receipts remain immutable; no pending operation
exists in this saved LAB. Contact mesh digest naturally changes with corrected
pose and old uncommitted frames must continue to fail consistency checks.

Files: WorldState.cpp, existing SrpOriginalImpactTest.cpp, this note; existing
Game isolated native build runner gets revision12 and mounts WorldState.cpp.
Game evidence captures the marked-target attempt before rotating client logs.
Preserve old revision11 artifacts and test results.

Validation: run cardinal/noncardinal pose and exact recorded controller sweep
on the old WorldState first to reproduce the defect, then corrected code; test
positive/negative yaw, translation, scale and existing bone override refusal.
For the recorded segment verify original radius10 contact and current scene LOS,
plus an opposite/wrong ray refusal. Repeat affected moving/pitch/drain/claws,
collision/dynamic guards and Hit/PartOne/impact regressions in the locked offline
toolchain; actual addon load and trade audit before packaging. Do not reinterpret
the earlier post-save Lord radius fixture as an exact engine frame.

Acceptance: a new human launcher entry, correct target/spell, persisted native
damage/healing/cost receipts and reconnect; offline success alone cannot mark
basic drain PASS. Reuse the same account/GeladoRun/world. Rollback: prior R11/S6
artifact pair with current forward-compatible saved LAB, retaining receipts and
items. Production rollback/deploy is outside this isolated correction.

## Offline validation

Old WorldState311e69c reproduced opposite-handed yaw and the actual contact
refusal,59assertions/2cases PASS in the explicit baseline mode. Corrected build
passed62assertions/2cases, including the same ray/radius against the pinned
original controller and scene LOS. Wrong-facing sphere still misses.
Other checks:234pitch,106moving-frame,130collision assertions/19cases,
418drain/claws/4cases,35,541regressions/75cases, addon load PASS. One optional
producer fixture was skipped without its separate mount; the pinned R49 scene
ran. Fifteen compiled overlay sources match the checkout after CRLF normalization.
Native SHA25682f52b9ff2585a0864873e9708f3688e7734e02a2b762e2e7f1842ed6771f52a.
The pre-existing WorldState typeid warning remains. Actual addon trade audit,
LAB boot and human basic drain are separate subsequent stages, not implied PASS.
