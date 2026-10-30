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

Tested official Linux toolchain: 388/388 C++ cases, including random seed
3286815900; 567 source/header hashes match compiled inputs. Real addon tests:
6 reload cycles, 60 actors/2 mock hosts, 4 SIGKILL windows + write denial,
actual compatible reader, unchanged 34668 NPC fixture files, original ACHR
19d5d:mihailhighfantasypack.esp and two mock subscriptions. These tests do not
execute Skyrim AI, rendering, collision or human combat.

Candidate SHA256 a8d980f72177bfa8782e63856a14780eec73f48e9fbf28bfd97e97da98ce234d.
Maintenance recovery reader SHA256
f1c9777b1cb798602873962da3c7a7d0d81cc394458787b9bdd23d7e87b6291c
is commit 70af137 plus resource/corpse reading and the NPC-only region validation exemption. It has no
live unload. Reopen gameplay only after separate capacity/acceptance checks.
Do not roll back to a reader that eagerly loads the whole world or heals NPCs.
Never restore an old world over newer progress. HF combat/rewards/respawn
remain OFF: residency is not collision/reward authority.

The fresh complete world restore caught parked player images rejected by NPC
region validation. Player exemption now precedes region/source evaluation.
Both final addons read all 1,582 player images and 34,809 NPC records from the
fresh stopped PUBLIC snapshot without changing file bytes or inventories.
