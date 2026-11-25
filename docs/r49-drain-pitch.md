# Drain aim pitch, 15 November 2026

Status PLAN_BEFORE_IMPLEMENTATION. Isolated R49 LAB.

Preflight and Obsidian protocol/index/state/map/system/decisions/regressions/
matrix consulted. Native2771291/PR7 and Game0f88c36/PR66 available. Existing
untracked AGENTS/scripts/cache remain present.

The actual R10 cast579010 at04:59:03.265UTC had matching original target33585739,
owner/session ready,28matching engine HitEvents and REFUSED ACK. The saved
server log records one SERVER_SEGMENT_MISSES_TARGET. No new impact receipt,
target remains50/145 and player100HP/67.4375Magicka. R10 event transport is
observed. Camera state and exact cast-frame positions were not logged, so the
particular geometric miss is not reconstructed from later saved positions.

Code comparison proves Contact uses only attackerAngle.z and always sets
the ray endpoint Z equal to the grip Z. movementGet.ts already sends actual
GetAngleX/Y/Z, and the native frame records them. A raised/lowered target can
therefore never be hit by this horizontal policy despite an accepted pitch.
Earlier R8/S4 Lord tests have three durable15damage/15healing/10cost receipts.

Reuse Contact, the existing accepted movement angles, original projectile
radius, current-frame validation, collision scene and durable impact writer.
For drains only, normalize signed pitch from angleX and trace the sphere in
3D along accepted yaw/pitch. Positive pitch points downward in the clockwise
Skyrim facing convention. Require pitch within90degrees. Retain the melee
horizontal segment and exact-frame behavior. Version the drain weapon policy.
No client hit point/damage/radius or new camera/target protocol is introduced.

On a missed drain log the native frame, from/to and original radius with a
two-second process-wide throttle. This records actual refused geometry for
subsequent investigation of third-person camera offsets. It contains no
session/nonces or profile/account information. Third-person camera parallax
and the actor proxy yaw convention require runtime comparison and remain open.

Files SrpOriginalImpact.cpp, existing SrpOriginalImpactTest.cpp, this note,
Game original-impact-runtime.js cancel reasons and existing isolated build/
trade/packaging runners. First run new inclined tests against2771291 to prove
the horizontal refusal, then run candidate inclines, moving-frame, collision,
drain/claws, Hit/PartOne regressions and actual addon trade smoke. Add the
unsupported-pitch reason to existing cancellation/refund handling.

Accept only matching target receipts and resource deltas in Skyrim. Preserve
R9/S5/R10 artifacts and all existing receipts. Pending old policy can cancel
through the existing weapon-change path. Rollback uses the prior artifact pair
and saved LAB snapshot. Production deployment follows the existing user policy.


Baseline2771291 reproduced the horizontal refusal for raised/lowered targets
in both forms,42assertions PASS. First candidate pitch run passed231 of232
assertions. One wall expectation was wrong because the Lord sphere touched
the slanted target before reaching the wall embedded25units from its centre.
The test wall now stands100units before the target, between caster and the
entire mesh. Production collision code was unchanged by this fixture correction.
Initial result is preserved as native-contact-r11/first-wall-fixture-result.json.
The complete candidate suite is running again.


Complete native validation PASS.234inclined-contact assertions,106moving-frame,
130collision/19cases,418drain/claws/4cases,35,541Hit/PartOne/impact assertions
in75cases and ADDON_LOAD_PASS. One optional producer fixture lacks its separate
mount, as in R9. The pinned R49 scene case executed. Native binary SHA256
eb6cf518673753550dd595a834697d93fa1c6d610adaabc7be49ad53d18220a0.

Second fixture revision exposed a wall height issue: at100units before the
target, its original128unit quad did not cross the inclined human trajectory.
The fixture extends that barrier by256units vertically. Both initial results
remain preserved. Candidate production code stayed the same during these
fixture corrections. Actual trade audit and LAB runtime are the next stages.
