#pragma once
#include <nlohmann/json.hpp>
class MpActor;

// Original-reference death observation and quarantine. Never grants XP/loot/
// respawn authority; native transport hosting is not hit proof.
struct SrpOriginalLifecycle
{
  static bool Managed(const MpActor& actor);
  static nlohmann::json Inspect(const MpActor& actor);
  static void RecordDeath(MpActor& actor, const MpActor* killer);
};
