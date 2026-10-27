#pragma once

#include "GetBaseActorValues.h"
#include <array>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>

class MpActor;

// One persisted SRP projection. Ranks are the input; caller-supplied bonus
// magnitudes are never authority. Base/racial values stay in their old owner.
struct SrpActorStats
{
  static constexpr const char* kProperty = "private.srpActorStatsV1";
  bool enabled = false;
  uint64_t revision = 0;
  uint32_t level = 1;
  std::string characterId;
  std::string profileId;
  std::array<uint32_t, 6> ranks{};

  static SrpActorStats FromProjection(const nlohmann::json& projection);
  static SrpActorStats Read(const MpActor& actor);
  nlohmann::json ToJson() const;
  bool SameProjection(const SrpActorStats& other) const;

  float EffectiveRank(size_t index) const;
  float HealthBonus() const;
  float MagickaBonus() const;
  float StaminaBonus() const;
  float CarryBonus() const;
  float PhysicalDamageMultiplier(bool ranged) const;
  float MagicDamageMultiplier() const;
  float MagicResistanceFraction() const;
  float ArmorBonus() const;
  BaseActorValues ComposeMaximums(BaseActorValues base) const;
};
