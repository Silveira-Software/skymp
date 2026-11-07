#pragma once
#include <nlohmann/json.hpp>
class MpActor;

// Server-only adoption of an ALREADY LOADED original ACHR. Does not spawn,
// teleport, respawn, grant rewards, change host ownership or enable combat.
struct SrpOriginalReference
{
  static nlohmann::json Adopt(MpActor& actor, const nlohmann::json& policy);
};
