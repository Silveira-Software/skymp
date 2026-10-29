# R37 experimental modifier composition

RELEASE_BLOCKED. This branch extends R36 commit
`8250d76f34de67f485bd782ecc9d96fd9d946399`; it does not replace PUBLIC's native.

The server composes race/base values, the reversible SRP rank layer, verified
inventory enchantments, supported temporary effects and unconditional passive
abilities/diseases. Distinct MGEFs coexist; one MGEF recast replaces its entry.
Changing maxima preserves absolute resources and never grants healing.
Legacy actors without the private SRP property retain their prior maxima path.

Inventory writers and equipment/spell changes reconcile resource maxima. The
binding exposes separate permanent/effective/current layers and an R37 capability
contract. Conditional supported-AV effects fail closed instead of assuming CTDA
is true. Their authoritative condition evaluator is still a release blocker.

The same atomic world record stores optional resource maximum metadata and
`srpStacked` effect rows. Old documents remain readable. Rollback must retain a
compatible reader and receipts; do not overlay old world data on newer progress.
Timers check actor instance, effect identity and expiry. Reload preserves absolute
resources, and malformed persisted maxima/effects are rejected.

Verified isolated Linux Release build: clang 20, existing official project CMake/
Ninja targets, image `srp-native-v129-openmp:20261011`, no network or host ports,
read-only production ESM. Candidate server addon SHA-256:

`d714e055bbb1cf98e45659d5e923a68db5ab2edb5b560c2fb398488a4a8718a9`

21 R36/R37 cases / 137 assertions passed. The wider native regression selection
passed 139 cases. Real addon tests cover two synthetic actors, actual hits,
ALCH/ENCH, saving and a fresh-process restart. Combined isolated Mongo/native
SIGKILL tests cover nine recovery windows. Results are automated runtime tests,
not observed Skyrim client acceptance. No Windows native client was rebuilt.

The matching gamemode/client work is on
`Silveira-Software/SKYRIMRP:codex/progression-r37-20261027` with source manifests,
tooling, migration/rollback notes and the Geladao/Elohim acceptance checklist.
Point allocation remains off; PUBLIC, production Mongo and CDN remain unchanged.
PUBLIC replacement requires new explicit authorization, fresh consistent backup
and restoration verification, and real Skyrim acceptance.


## 28 October: scoped CTDA evaluation, still BLOCKED

CTDA is now parsed per EFID for SPEL/ENCH, with exact 32-byte validation.
The native evaluates authoritative Subject GetItemCount (47), GetIsRace (69)
and IsInInterior (300), all six comparisons and AND/OR groups. Form arguments
are mapped through the owning plugin. False means inactive; unsupported means
Unknown and prevents a complete modifier projection. Unsupported flags, globals,
run-on targets, malformed data and nonfinite values never become a free bonus.
Reading the binding reconciles condition changes against absolute resources;
its durable generation barrier remains in the gamemode.

The read-only five-official-ESM audit found 123 managed AV MGEFs and 58
conditional MGEF/SPEL/ENCH records, with 19 function indices. These are record
counts, not evidence of full CTDA coverage or of effects used by every actor.
ALCH/INGR effect owners were also scanned, with no additional conditional
managed-AV records in that official set. Third-party plugins are not covered.

Weather, time, quest/global/perk queries, caster/target contexts and active
effect source provenance are still incomplete. The current durable active
effect entry stores MGEF but not its originating SPEL/ALCH/ENCH effect index.
Do not enable allocation or claim all spells are handled from these tests.
The global R37 allocation flag stays OFF. No PUBLIC change was made.


## 28 October: durable effect origin V1 (offline candidate)

Later increment supersedes the origin limitation above for newly persisted ALCH
consumption effects. `effects[].srpProvenanceV1` records canonical source/MGEF,
winning plugin, SPEL/ALCH/ENCH type, EFID index, caster/target character identities,
instance/operation IDs, epoch expiry and explicit recast/overlap policies. All
source values are checked against real records. New origins are never guessed
for legacy rows. The internal SPEL/ENCH API is validated; network spell-cast
integration and cross-caster identity durability are still pending.

Recast replaces only its own source/EFID/caster/target row. Distinct casters retain
independently expiring rows but a shared MGEF contributes its strongest absolute
modifier once. Legacy rates retain one row per actor value. Unsupported CTDA
remains Unknown. Added GetIsID (72), GetEquipped (182), HasSpell (264) and
WornHasKeyword (682); equipment checks actual positive-count inventory variants.
Explicit reference context resolves only the matching actor/caster. No guessed
quest stages, globals, perks, weather or server time.

A real new test found in-memory ChangeForm restoration rescaling HP twice when
no serialized maxima envelope was present. GetChangeForm now includes the same
`srpResourceMaximumsV1` snapshot as world serialization. Its format is unchanged.
The restore test preserves 80 HP instead of incorrectly reducing it to 68.8889.
New effect expiry uses epoch milliseconds, independent of legacy locale text.

Candidate addon SHA-256 (not installed):
`5d473c5f1645f2d8790064efb3a2e05adbd6b4d305f30a6815ffba095e374e17`.
R37-specific and broader native regression results live in the matching gamemode
PR #60 evidence. Three additional SIGKILL windows cover one atomic world image
containing inventory, origins and maxima; they do not imply a Mongo/world
transaction. Fresh-process addon restore also preserves exact origin metadata.

Rollback must keep a V1-compatible reader, receipts and current progress. An old
reader is rejected even if origins have expired; do not discard rows to downgrade.
`effectProvenanceVersion=1` is mandatory for the gamemode allocation capability.
Allocation OFF, RELEASE_BLOCKED; no production replacement or Skyrim human test.

## R37.2, 28 October: gated self casts and equipment origins

The experimental addon now exposes `castSrpSpell(user, spell, opId,
characterId, queryOnly)`. It resolves the authenticated user's actor and accepts
only learned/equipped, supported Self/FireAndForget spells. Source/EFID/MGEF
conditions, resource availability and cast interval are validated before one
ChangeForm transaction records cost, effects and `private.srpSpellReceiptsV1`.
The gamemode waits for the saved-generation barrier before ACK. A duplicate or
fresh-process query returns the existing receipt without another debit/effect.
NoRecast and DispelEffects are rejected until their policies are proven.

This path is OFF by default. It requires an explicit RECORD_BASE_ONLY_V1 cost
policy, which is not the complete vanilla cost formula. Player/NPC targeted
impacts and cross-actor recovery are not implemented by this path. Animation
packets remain animation events and do not confirm an application. Engine/server
composition and real Skyrim acceptance remain blocking gates.

Verified inventory variants, slots, ENCH/EFIDs and conditions feed the existing
modifier composer. A derived `private.srpEquipmentOriginsV1` ledger tracks
individual equipment contributions and marks absent origins REMOVED. It does not
invent physical UUIDs for indistinguishable vanilla copies or duplicate the
inventory/effect executor. Legacy effect origins are not inferred.

Added native CTDA contexts: GetDead, GetInFaction, GetInWorldspace, HasPerk,
HasKeyword, GetActorValuePercent and WornApparelHasKeywordCount. HasPerk requires
a scoped, explicitly complete SRP-PERKS-V1 ledger; no production producer was
invented. Missing context stays Unknown. The installed-record audit classifies
60 records as 2 structurally supported, 20 requiring runtime authority and 38
blocked, across 347 condition rows. These counts are not true-condition results.

Recast timers survive a transaction abort, while instance/expiry checks prevent
an old callback removing a committed replacement. Death removes only eligible
provenanced temporary origins and respects NoDeathDispel. Re-adding a faction
reactivates a negative-rank tombstone and preserves existing valid ranks.

Official isolated clang 20/CMake/Ninja build: 44 R36/R37 cases, 333 assertions,
zero failures. Candidate addon SHA-256 (never installed in PUBLIC):
`73b81a29b2b97e4e1256968ae75560caec8fcb3dc856625b1c54246c16e1cf4e`.
The gamemode PR #60 contains the exact-byte runtime, restart, SIGKILL, Mongo,
launcher and source-parity evidence. New reader capability checks prohibit
rollback to a reader that cannot preserve the SPEL receipts or ENCH ledger.
Keep current progress and all receipts; do not restore an old world over it.

PUBLIC R35/hotfix R3, API, Mongo, CDN and launcher remain unchanged. Allocation,
self-spell flags and R37 release remain blocked. No real Skyrim R37 acceptance
or production deployment is claimed.
