# R49 — disabled original precedes periodic harvesting

R16 fixed cold interest delivery. The actual R16/R12 engine entry then kept
book108a48 and chest8eab4 disabled and bound the original Afflicted with
alias0/no protections. Door6b78b still became enabled. Its original reference
and base have no enable-parent/script/linked-reference override in the owned
49-plugin installation.

The client sequence explains the remaining mismatch: R12 RemoteServer applies
the canonical disabled state on create, then FormView.applyAll calls the old
harvest helper every666ms. For a non-flora object, isHarvested=false plus
isDisabled=true causes that helper to call enable(true). The other two objects
skip the full view and therefore stay disabled.

R13 applies an explicit boolean canonical/legacy disabled property to a raw
original non-actor during the existing view update, and skips harvesting only
while that original is authoritatively disabled. Explicit canonical false
wins over legacy true. No property means the former behavior; actors, dynamic
and encoded IDs keep the old path. No new timer/channel is added. Existing
open state, catalog, ownership, native R16/S11 and platform DLLs are preserved.

Nine directed tests execute actual compiled RemoteServer create followed by
actual FormView.applyAll and ModelApplyUtils. R12 disables then re-enables;
R13 stays disabled across eight periodic passes, reconciles a later engine
reset, preserves canonical false/legacy behavior, malformed/unknown state,
harvesting of enabled objects, actor/dynamic exclusions and the social guard.
The real compiled SendInputs constructor passes. Patch reverses byte for byte.

Client SHA-256:
`4402b405c94f71a27deb63eb56b31cd1d0d16f74fe200d9467bb8f3f1794c4b0`.
Manifest: `12d433ec321811fb7a0e6a1f3477e001cc52cb105a948ceae9b50b5703da6240`.
At this record the official package is prepared; installation and engine
observation remain pending. Human feeding remains NOT TESTED; no PUBLIC deploy.
