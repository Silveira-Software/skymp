# Recheck werewolf claw contact when the intent is saved

Plan recorded on 16 November 2026 before editing. Obsidian preflight and
project notes, PR 7 at Native 8eaa72c and PR 66 at Game 1506b4d were checked.
The previous durable combat path uses the exact admission pose for melee.
The moving drain work in `docs/r49-moving-drain.md` already checks contact
again against the accepted pose while preserving the write and receipt rules.

In the R13 LAB the human cured vampirism and activated werewolf form.
The native accepted a real claw event on original Boneman 33585739. The
operation's admission proof shows clear contact within the original reach
of 150. Before the first application it reached `SRP_IMPACT_FRAME_CHANGED`.
That check follows the target generation, saved-state, session, weapon
and current contact checks. The intent was cancelled and the Boneman stayed
at 42 of 145 health. Later saved poses help explain why exact equality cannot
be relied on, but are not the precise frame seen during the refused Apply.

Extend the existing current-contact path to werewolf claws. Retain both
geometry checks, the original horizontal trajectory and reach, the same
scene and world, and a clear trace of the current blockers. Preserve exact
target save generation, identity, host, session, intent expiry, weapon,
transformation, reserve, damage formula and the durable target receipt.
Ordinary weapons keep the exact admitted proof. Version the claw validation
policy in the existing weapon snapshot so an old pending intent follows the
existing weapon-change cancellation. Completed receipts still reconcile first.

Files are `SrpOriginalImpact.cpp`, its existing native test file and the R49
LAB build runner. Test the reported valid movement against the R13 baseline
and candidate. Exercise refusal after turning away, a target save and a
scene replacement. Verify a successful retry writes one target receipt.
Run the existing impact and supernatural tests before packaging. Human
acceptance requires a real saved hit and verification after reconnect. The
feeding test needs a separate eligible humanoid corpse. Revert the server
pair to R13/S8 using the current snapshot if the LAB package fails boot.

## Validation

R13 reproduced the frame rejection across 47 assertions. R14 passed 55
assertions covering the four scenarios, including one saved target receipt
and duplicate acknowledgement after a valid reposition. The 75 impact and
server regression cases passed 35,541 assertions. The existing save ACK,
claw and drain suite, yaw, pitch and collision tests passed. One optional
collision producer fixture was skipped. All 15 overlay source hashes matched.
The native module loaded from the compiled binary with SHA-256
`f035cb011dd0fa099277cf0fc55b30fa951ad6f00ab74d6fcf636c740a303d3c`.
Damage from a claw attack in Skyrim still requires the LAB retest.
