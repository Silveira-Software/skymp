# R38.5 NPC residency — 2026-10-30

Opt-in only: configure srpNpcAdmissionV1 first, then srpNpcLiveUnloadV1
(schemaVersion=1, enabled=true, graceMs=30000, maxNpcsPerHost=64).
No new world document format. PUBLIC installation requires a fresh consistent
backup, restore check and explicit owner maintenance authorization.

Alive FAUNA/HUNTING_NATIVE actors and approved, durably adopted original
references may leave memory after the last nearby observer leaves. Profiles,
unknown ownership, quests, scripts/VM handles, active triggers, callbacks,
active effects, dead/deleted actors and outstanding operations stay protected.
Passive outside-trigger subscriptions do not pin an otherwise idle actor.
Save the exact current ChangeForm and await its latest durable generation
before detaching subscriptions, releasing admission or dropping memory.
Reload preserves identity, inventory and resource percentages. Loading a corpse
with admission enabled cannot schedule an implicit respawn.

The existing idx-only transport has no lease generation/HostStop ACK. Retired
NPC indices remain quarantined until every connection that could know them
has disconnected. Bound 65536; saturation retains the actor, never aliases an
index. Admission and network index budgets are independent.

Tested official Linux toolchain: 387/387 C++ cases, including random seed
3286815900; 567 source/header hashes match compiled inputs. Real addon tests:
6 reload cycles, 60 actors/2 mock hosts, 4 SIGKILL windows + write denial,
actual compatible reader, unchanged 34668 NPC fixture files, original ACHR
19d5d:mihailhighfantasypack.esp and two mock subscriptions. These tests do not
execute Skyrim AI, rendering, collision or human combat.

Candidate SHA256 a07a095610fe6f76acdc819351fedc6f6668e097a13e8f1a3c5d83234bb1309f.
Maintenance recovery reader SHA256
b4488acb898b5fc566e3ed76da6a7a54a071fe054da1e80dd5fa720819bc3aff
is commit 70af137 plus only the resource/corpse read correction. It has no
live unload. Reopen gameplay only after separate capacity/acceptance checks.
Do not roll back to a reader that eagerly loads the whole world or heals NPCs.
Never restore an old world over newer progress. HF combat/rewards/respawn
remain OFF: residency is not collision/reward authority.
