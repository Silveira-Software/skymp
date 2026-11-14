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

The Windows workflow 43541714554 passed for commit
67101f8af5d81582c50c972c138e576082ca1102. Both the eight hook cases and the
seven process cases passed. The previous source failed with the same hook
context error. The three outputs were rehashed after download.

- SkyrimPlatform.dll `21e0d13f9b116bcc85ead88d6c9e2a5126ad0cc08498c0bdd257e0e135ad63d5`
- SkyrimPlatformImpl.dll `977a19adab247f2ec1ced634541071ab579371f0d06392dc3236f921ed24f77e`
- SkyrimPlatformCEF.exe.hidden `c19323e5898c72ec4cf85faa51ba78bebcc2ab7e2df5b1377202f374449219ef`

The private launcher transaction installed the two platform packages, restored
the previous package and installed R4 again. Each round verified 5,675 managed
files and 46 plugins. The installed R4 manifest is
`bc9721010eaa90d02901247ee182fda58f029c16ec92699eb75895f7d513d809`.
The same LAB resumed GeladoRun from its current database and world. Engine
recovery and the subsequent transformation still await human re-entry.


## Preparação física de combate R49

A produção schema2 recusava os dois RACE de transformação por não terem flag Playable. Commit d47aa24 admite para basePlayer apenas cdd84:Skyrim.esm/WerewolfBeastRace e283a:Dawnguard.esm/DLC1VampireBeastRace, com os demais vínculos e hashes existentes.29testesPASS e NIFs instalados produziram controladores reais de Nord, Lobisomem e Lorde.

O produtor adicionava marcadores sem Havok ao conjunto de bloqueadores. O AutoLoadMarker6459:Dawnguard.esm foi classificado non-colliding-model e simultaneamente bloqueava o frame. Corrigida a seleção para descartar apenas os registros/modelos cuja leitura confirmou ausência de corpo físico. Referências desconhecidas conservam os gates e objetos móveis continuam auditados. A cena regenerada conserva a mesma geometria, perfis e classificação por referência e retira somente esse marcador. O mesmo addon final passou de frame indisponível a contato CLEAR com o original7a4b:Dawnguard.esm, guardando outbox e cancelando antes de dano. Não houve reconstrução do addon nem homologação humana de impacto. Prova docs/r49-physical-target-regression.json.
