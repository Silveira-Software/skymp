# R49 — durable inactive original bodies

`untrackedPhysics` used to reject even a loaded original object explicitly
disabled and durably saved by the server. The new policy is opt-in through
`allowDurableInactiveOriginalBodies: true`; the default retains the old
refusal. It also requires the verified physical baseline.

Every listed object must be a loaded non-actor original reference with the
matching scene binding, original base, cell, position and angle. It must be
disabled, not deleted, and have a positive generation whose save is confirmed.
Missing, cold, dirty, active, moved or mismatched references still refuse the
frame. Duplicate/invalid IDs and incomplete audits refuse too. The original
collision geometry remains in the scene as a conservative blocker.

The existing `isDisabled` binding and built-in client `disabled` projection
are reused. No new setter or client command is introduced. A cold load cannot
reuse generation zero as an ACK: reasserting the same fixture metadata through
the existing writer must produce a new confirmed generation. The client must
still be observed disabled before requesting a human attack in the LAB.

Validation:

- The f69c3cf baseline reproduced refusal of a valid disabled original
  container: 11 assertions, one case.
- Candidate: 46 directed assertions; additional cold-load coverage passed
  61 assertions across two cases with unchanged production overlays and the
  same addon hash.
- Existing regressions: 35,541 assertions / 75 cases; moving drain106,
  pitch234, yaw62, saveACK78, claw contact55, supernatural418 and collision130.
  The optional producer fixture was skipped; the pinned R49 scene ran.
- The real-addon trade smoke passed all three groups. Addon load passed.

Addon SHA-256:
`e5a621008ceb467a3f869e73af415b002a40e06d3fcd0815300d9657673ec50e`.
The gate tests use explicit unit-only triangle bindings; they do not claim
real engine poses or human feeding. The separate shader-only producer change
passed33 tests including the owned pinned lantern asset.

Two fixture mistakes were preserved in the local evidence: the first selected
an unsupported MSTT reference; the cold test initially called a protected
helper and was corrected to the public inventory writer. Neither required
relaxing the production guard.

R14 human claw damage and reconnect remain accepted in their recorded scope.
R15 LAB setup, client observations, actual humanoid kill, feeding and replay
are pending. PUBLIC has not been updated.
