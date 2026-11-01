#pragma once
#include <nlohmann/json.hpp>
#include <string>
class MpActor;

// Original-reference death observation and quarantine. Never grants XP/loot/
// respawn authority; native transport hosting is not hit proof.
struct SrpOriginalLifecycle
{
  static bool Managed(const MpActor& actor);
  static nlohmann::json Inspect(const MpActor& actor);
  static void RecordDeath(MpActor& actor, const MpActor* killer);
  // Authoritative death written by the impact service after it applied lethal
  // damage. Idempotent per operation: replaying the same operation returns the
  // stored receipt instead of killing twice. rewardsEnabled is only set when the
  // caller proved it resolved the drops from the plugin.
  static nlohmann::json RecordAuthoritativeDeath(MpActor& actor, const MpActor& killer,
    const std::string& operationId, double damage, bool dropsResolved);
};
