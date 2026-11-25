# Drain admission while a save is pending — plan before editing, 16 November 2026

Isolated R49 LAB only. Preflights, Obsidian protocol/index/state/map/system/
decisions/regressions/matrix and PR7/PR66 consulted. Native3535560 and Game8f75cc4
are remote. Untracked AGENTS/scripts/cache remain untouched.

Actual R12 casts03:07:38.436Z and03:07:40.430Z used camera0, spell579008 and
target33585739. The first cast has real casting-node observations; both native
requests refused PRIOR_SAVE_PENDING before any contact evaluation. No new
receipt/cost/heal: target50/145, player Magicka70, three APPLIED receipts each.
The same transient refusal previously occurred on R11. Native Begin must retain
its durable precondition. The packet wrapper currently sends REFUSED whenever
no operation exists after the event; the shipped client discards its action on
REFUSED, so a normal asynchronous save terminates an otherwise live channel.

Reuse the existing PENDING ACK and client250ms retransmission of the same action
ID. After native authentication, packet/session/managed-target/spell validation,
and the interrupt branch, detect an unsaved attacker or target before firing a
new drain admission. Send PENDING and return without Begin, minting an identity,
charging cost, creating a timer or changing world state. A fresh live client
packet must retry after save; there is no server-side authorization to apply
damage later. Keep5s client expiry, interrupt, session and durable writer gates.
Other terminal refusals and melee/claws behavior remain unchanged.

Files: SrpOriginalImpact.cpp and existing SrpOriginalImpactTest.cpp; Game existing
locked build runner revision13, focused emitted-client ACK test and evidence.
No new protocol fields, camera/node trust, mesh, radius, origin, input or health
change. The real cast origin was98.2133 units above feet vs proxy76.014; that
independent geometry limitation remains unresolved and is not fixed by this ACK.
Actual target Capsule01 agrees with corrected clockwise yaw. Do not claim an
origin change or basic drain PASS from this durability correction.

Test old bytes first: pending attacker/target produces terminal REFUSED and the
existing client drops its action. Candidate: PENDING preserves the same action
with no ledger/cost/identity or extra target damage; saved retry admits once,
same-ID duplicate charges once, interrupt reconciles/cancels normally; stale
session cannot request PENDING or admission. Retain other terminal ACK semantics.
Run affected native packet/drain, yaw/pitch/moving/collision, Hit/PartOne/impact
regressions and actual addon load/trade audit before packaging. Scene fixture
and generic synthetic tests remain distinct from Skyrim acceptance.

After validation prepare one native/gamemode pair with exact reverse patch and
audited pins. Apply only with Skyrim/launcher closed and a fresh current world+
Mongo LAB snapshot, preserving password, GeladoRun and receipts. PUBLIC is never
deployed. Rollback to R12/S7 against current forward-compatible saved data,
without restoring old player progress or deleting ledgers. Human acceptance
requires the transient ACK to keep the channel alive and a separately proven
contact/cost/damage/heal path; incomplete geometry stays explicitly pending.

## Test results

The first baseline fixture assumed Begin emitted only an ACK. It also emits
Magicka escrow after successful admission. The preserved first report records
that fixture failure. Filtering for the actual drain ACK fixed the fixture.

The R12 baseline reproduced terminal refusal with either actor awaiting save
and passed 80 assertions. R13 passed 78 assertions for the same paths, including
the same-ID retry and refund on release. The audited R10 client preserves its
action on PENDING and interrupts on release or five-second expiry. The native
regressions passed 35,541 assertions across 75 cases. Yaw, pitch, moving drain,
collision, claws and drain checks passed. One optional collision producer fixture
was skipped. All 15 overlay hashes matched and the addon loaded successfully.

Addon SHA-256 `58781acee721b7dd8033f555b821010f0097adc1198c77cdbd7dd759379abe99`.
