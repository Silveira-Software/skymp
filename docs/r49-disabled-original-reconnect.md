# R49 — disabled originals after cold load

The R12 human entry bound the exact Afflicted original with alias0, no
protection flags and no proxy. Its three inactive scene objects still read
enabled in Skyrim. The server saved all three disabled. Inspection found a
warm/cold discrepancy: Disable deliberately retains original non-actors in
the interest grid, while ForceSubscriptionsUpdate skipped every disabled
reference during ApplyChangeForm after loading the saved image.

R16 lets those original non-actors rejoin the same interest grid while staying
disabled. The existing CreateActor VisitProperties sends isDisabled=true to
the client. No new channel, property protocol or installation system is added.
Disabled actors, dynamic references and deleted disabled originals remain
excluded. The R15 physical guard, saved generations and static geometry are
unchanged. The R12 client door projection and ownership checks are retained.

Validation on the pinned toolchain:

- Baseline d10dfbb reproduced missing cold messages for an original barrel
  and all three feeding-scene references:37 assertions/two cases.
- R16 warm/cold/second-connection and exclusions:57 assertions/two cases.
- Object-reference and original-stat regressions:1533 assertions/17 cases.
- Existing regressions35541/75, moving drain106,pitch234,yaw62,saveACK78,
  claw55,supernatural418,inactive physics61 and collision130 passed.
  One optional producer fixture was skipped; the pinned physical scene ran.
- The real addon sent exactly one canonical disabled property for each of
  6b78b/8eab4/108a48 in both warm and separate cold processes, with the owned
  49-plugin Bthardamz scene. Three real-addon trade groups passed.
- The same R16 addon admitted contact in the verified Bthardamz scene after
  the inactive objects were saved; the intent was cancelled before damage.

Addon SHA-256:
`f976c36b0519d0422752e297623d20ec4801716822282136b0436025ed2f7340`.
These are offline tests, not a new engine or feeding acceptance. Actual
R16 client observations, humanoid kill, feeding and replay remain pending.

The earlier apparent health loss compared a maximum of228 with current30.
The saved state before R12 already held30/228. ApplySrpOriginalStats preserves
absolute resources, and its reload regression passed. No health reset, healing
or actor-stat change is part of R16. PUBLIC has not been updated.
