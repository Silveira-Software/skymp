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

`bd1bf40a0e0a37ad2886d6dcbb438e57f63068c31216254b5e25bfab32b6c0dd`

19 R36/R37 cases / 109 assertions passed. The wider native regression selection
passed 137 cases. Real addon tests cover two synthetic actors, actual hits,
ALCH/ENCH, saving and a fresh-process restart. Combined isolated Mongo/native
SIGKILL tests cover nine recovery windows. Results are automated runtime tests,
not observed Skyrim client acceptance. No Windows native client was rebuilt.

The matching gamemode/client work is on
`Silveira-Software/SKYRIMRP:codex/progression-r37-20261027` with source manifests,
tooling, migration/rollback notes and the Geladao/Elohim acceptance checklist.
Point allocation remains off; PUBLIC, production Mongo and CDN remain unchanged.
PUBLIC replacement requires new explicit authorization, fresh consistent backup
and restoration verification, and real Skyrim acceptance.
