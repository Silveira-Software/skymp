#pragma once
#include <Networking.h>
#include <nlohmann/json_fwd.hpp>
#include <string>
class PartOne;
class MpActor;
class WorldState;
// Additive overload preserves the original public header and three-argument
// ABI. Implementation, outbox and consumer remain SrpOriginalImpact.
namespace SrpOriginalImpact {
nlohmann::json Begin(PartOne&, Networking::UserId, uint32_t,
                     const std::string&, uint32_t spellId);
nlohmann::json Apply(PartOne&, Networking::UserId, const std::string&, bool channelObserved);
bool IsDrainSpell(WorldState&, uint32_t source);
bool OwnsDrainResources(const MpActor&);
}
