#pragma once
#include "GetBaseActorValues.h"
#include "libespm/NPC_.h"
#include <array>
#include <nlohmann/json.hpp>
#include <optional>

class MpActor;

// Uses the existing BaseActorValues, template resolver and ChangeForm. The
// default/player resource path is unchanged. No client-supplied level, class,
// maximum or encounter level is accepted by this interface.
namespace SrpOriginalStats {
constexpr auto kProperty = "private.srpOriginalStatsV1";
nlohmann::json Preview(const MpActor& actor);
std::optional<BaseActorValues> Read(const MpActor& actor);
BaseActorValues Allocate(BaseActorValues base, uint16_t level,
                        const std::array<uint8_t, 3>& weights,
                        float healthLevelBonus, uint32_t resourceLevelUp,
                        const std::array<int16_t, 3>& offsets = {});
// Base-NPC GetLevel arithmetic only. AE uses its global player's NPC level in
// this path; final per-actor CELL/EZ resolution is separate and not implied.
// This pure function neither obtains authority from a client/host nor produces
// a durable encounter context. Preview still refuses every PC-scaled actor.
uint16_t ResolveLevel(const espm::NPC_::Data& npc,
                      std::optional<uint16_t> pcLevel);
// Read and validate an existing receipt before checking this compatibility.
// Legacy fixed-level snapshots used a hardcoded level-up budget of ten.
bool SameSnapshot(nlohmann::json existing, nlohmann::json proposed);
}
