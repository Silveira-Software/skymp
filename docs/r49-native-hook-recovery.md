# R49 — Papyrus hook registration remains blocked

The human R3 session entered GeladoRun through the official LAB launcher. The
client supplied both callbacks required by Handler.cpp, then the engine logged
`Trying to add hook inside hook context`. Its durable transformation recovery
remained PREPARED; no successful transformation or recovery is claimed.

Preflight PASS on 13/11/2026: Native branch codex/r38-native-validation-20261029,
HEAD 1c69c5d, PR7 available; Obsidian index/state/map/protocol/decisions, launcher,
supernatural, regression and truth notes consulted. Game changes R2/S1 are in
8467a8b/003e01a (PR66). R3 JS/package work is pending commit in that checkout.

Cause in the production source: Hook::Enter increments addRemoveBlocker before
the Papyrus no-match and queued-dispatch early returns. Hook::Leave increments
it before returning for Papyrus. These paths never decrement it. Subsequent
AddHandler/RemoveHandler calls therefore see a permanent execution context.

Plan before edits: use an exception-safe scoped counter in Hook.cpp; balance
all early returns and enqueue/dispatch failures, and apply the counter around
the actual deferred callback too. Keep filtering, event payloads, native ABI,
handler ordering and the prohibition on mutation inside a callback. Test the
production Enter/Leave/Add/Remove bodies with a controlled scheduler, including
empty/unmatched events, deferred execution, nested mutation and exceptions.
Run the same harness on the previous source to reproduce its leak. Reuse the
existing Windows build workflow and process-isolation checks. Build only the
private client DLL candidate; do not modify PUBLIC, API, Mongo or CDN.

The installed baseline implementation is a62fec22a37974f39462b4a936c883a052ad6cfea6e45bf0fbf7a120ff311c53;
the prior isolated candidate is 354df75525882fff284bce9c953a04cba660283532e1eb4f088b9f25f8c43dea.
Their different hashes are not evidence of compatibility. Record the exact
new workflow commit, tests, artifacts and private manifest before installation.
Retain the previous managed binaries and the current character/world receipts.
Install through the official LAB transaction after game/launcher closure.
Acceptance requires a real fresh race event, two loaded frames, durable world
confirmation and resource ACK; only then request a new human transformation.
Rollback restores the previous client package and keeps the current saved data.
