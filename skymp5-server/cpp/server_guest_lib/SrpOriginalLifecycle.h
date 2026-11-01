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
  // Durable death request written in the same ChangeForm transaction as the
  // lethal health write. It stays DYING until RecordDeath (inside the corpse
  // transaction) finalises it, so an interrupted death is resumed, not repeated.
  static nlohmann::json BeginAuthoritativeDeath(MpActor& actor, const MpActor& killer,
    const std::string& operationId, double damage);
  // True while a durable death request has not yet been turned into a corpse.
  static bool AwaitingCorpse(const MpActor& actor);
};
