#pragma once
#include <nlohmann/json.hpp>
#include <optional>
#include <cstdint>

class MpActor;

namespace SrpOriginalEncounter {
constexpr auto kProperty = "private.srpOriginalEncounterV1";
// Server-only entry. The addon must verify that entrant is the actor attached
// to an authenticated live user. No caller-supplied level or zone is accepted.
nlohmann::json Capture(MpActor& actor, const MpActor& entrant);
std::optional<nlohmann::json> Read(const MpActor& actor,
                                  bool requireDurable = false);
uint16_t ZoneLevel(uint16_t pcLevel, uint8_t minimum, uint8_t maximum,
                   uint8_t flags, float scaling);
}
