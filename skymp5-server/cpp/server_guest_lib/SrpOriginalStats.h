#pragma once
#include "GetBaseActorValues.h"
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
                        float healthLevelBonus);
}
