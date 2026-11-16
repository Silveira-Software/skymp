# R49 aimed drain routing

Status: client source and private bundle tested; installation and engine trace pending.

The R5 human session adopted the original NPC with alias count zero and a single
V16 lease. Its client health fraction matched the saved server fraction. Two
human drain casts did not create an original-impact ledger. The first equipment
update was rejected because an old drain stage remained in the other hand.
Replacing that hand with Flames allowed the current drain equipment update,
but the next cast still did not produce an authoritative impact.

The Platform `TESSpellCastEvent` handler obtains `target` from
`caster->GetMagicTarget()->GetTargetStatsObject()`. The Actor implementation
returns itself. `MagicSyncService` previously preferred that truthy self target
over the crosshair, and `HitService.beginDrain` rejected the player target before
opening a channel. The exact runtime event payload still needs the R6 trace.

The existing MagicSyncService now treats a target equal to the caster as absent
for aimed drain routing. It then reads the real crosshair reference. A distinct
event target is preserved; missing targets never authorize a fabricated victim.
The legacy path for other spells is unchanged. Native negotiation, geometry,
cost, owner, state and durable impact validation remain required.

The existing services record bounded cast/request/ready/ack/stop diagnostics.
They do not log session values, operation nonces or arbitrary server error text.

Validation in Game PR66 tooling:

- Seven cases execute the actual R5/R6 compiled MagicSyncService and HitService
  methods, reproduce the old self-target rejection, and verify target selection,
  missing targets, dual casting, missing negotiation, legacy spells and redaction.
- Nineteen existing client transport/claws/drain cases pass, including nonce
  retry, ACK, interruption, disconnect and Vampire Lord single-cast behavior.
- The real compiled SendInputs constructor and syntax transpilation of both
  TypeScript services pass. Engine calls in VM cases are test doubles.

Private R6 bundle SHA256:
`26397d765c7fbdab666bc470e0c51703e7326d9df7f03ad9bb201130c8a53ae9`.
Game build script preserves all unrelated R5 bytes and reverses exactly.
No native addon or Platform DLL was rebuilt or installed for this source change.

Game evidence: `ops/public-regression-recovery-20261104/evidence/r49/client-r6-drain-target.json`.
Rollback uses the saved R5 client package and keeps the latest world/DB receipts.
PUBLIC has not been changed. Authoritative damage, healing and cost remain
unaccepted until a real cast produces durable runtime evidence.

Primary API reference:
https://github.com/CharmedBaryon/CommonLibSSE-NG/blob/main/include/RE/A/Actor.h
