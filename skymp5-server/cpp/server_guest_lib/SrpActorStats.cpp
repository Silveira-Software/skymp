#include "SrpActorStats.h"
#include "MpActor.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace {
constexpr std::array<const char*, 6> kNames = {
  "vigor", "strength", "dexterity", "intelligence", "willpower",
  "constitution"
};
uint64_t Integer(const nlohmann::json& value, uint64_t maximum)
{
  if ((!value.is_number_integer() && !value.is_number_unsigned()) ||
      value.get<double>() < 0 || value.get<double>() > double(maximum)) {
    throw std::runtime_error("SRP_STATS_INVALID_INTEGER");
  }
  return value.get<uint64_t>();
}
}

SrpActorStats SrpActorStats::FromProjection(const nlohmann::json& p)
{
  if (!p.is_object() || p.value("schemaVersion", 0) != 2 ||
      p.value("rulesVersion", std::string()) != "SRP-PROGRESSION-A1") {
    throw std::runtime_error("SRP_STATS_INVALID_SCHEMA");
  }
  SrpActorStats out;
  out.enabled = true;
  out.revision = Integer(p.at("revision"), 9007199254740991ULL);
  out.level = uint32_t(Integer(p.at("level"), 200));
  out.characterId = p.at("characterId").get<std::string>();
  out.profileId = p.at("profileId").get<std::string>();
  if (!out.revision || !out.level || out.characterId.empty() ||
      out.characterId.size() > 100 || out.profileId.empty() ||
      out.profileId.size() > 20) {
    throw std::runtime_error("SRP_STATS_INVALID_IDENTITY");
  }
  if (!std::all_of(out.profileId.begin(), out.profileId.end(), [](char c) { return c >= '0' && c <= '9'; }) ||
      std::stoull(out.profileId) == 0 || std::stoull(out.profileId) > 2147483647ULL) {
    throw std::runtime_error("SRP_STATS_INVALID_PROFILE");
  }
  const auto& ranks = p.at("attributes");
  if (!ranks.is_object() || ranks.size() != kNames.size()) {
    throw std::runtime_error("SRP_STATS_INVALID_RANKS");
  }
  for (size_t i = 0; i < kNames.size(); ++i) {
    out.ranks[i] = uint32_t(Integer(ranks.at(kNames[i]), 200));
  }
  return out;
}

SrpActorStats SrpActorStats::Read(const MpActor& actor)
{
  const auto& fields = actor.GetDynamicFields().GetAsJson();
  if (!fields.contains(kProperty)) {
    return {};
  }
  const auto& stored = fields.at(kProperty);
  if (!stored.is_object() || stored.value("schemaVersion", 0) != 1) {
    throw std::runtime_error("SRP_STATS_CORRUPT");
  }
  auto stats = FromProjection(stored.at("projection"));
  if (stats.profileId != std::to_string(actor.GetProfileId()) ||
      !fields.contains("srpActiveCharacterId") ||
      fields.at("srpActiveCharacterId") != stats.characterId) {
    return {}; // A previous character's projection grants nothing to this one.
  }
  stats.enabled = stored.at("enabled").get<bool>();
  return stats;
}

nlohmann::json SrpActorStats::ToJson() const
{
  nlohmann::json ranksJson = nlohmann::json::object();
  for (size_t i = 0; i < kNames.size(); ++i) {
    ranksJson[kNames[i]] = ranks[i];
  }
  return { { "schemaVersion", 2 }, { "rulesVersion", "SRP-PROGRESSION-A1" },
           { "revision", revision }, { "level", level },
           { "profileId", profileId }, { "characterId", characterId },
           { "attributes", ranksJson } };
}

bool SrpActorStats::SameProjection(const SrpActorStats& other) const
{
  return ToJson() == other.ToJson();
}

float SrpActorStats::EffectiveRank(size_t index) const
{
  if (!enabled) {
    return 0.f;
  }
  const auto rank = float(ranks.at(index));
  return rank / (1.f + rank / 40.f);
}
float SrpActorStats::HealthBonus() const { return 3.f * EffectiveRank(0); }
float SrpActorStats::MagickaBonus() const { return 3.f * EffectiveRank(4); }
float SrpActorStats::StaminaBonus() const { return 3.f * EffectiveRank(5); }
float SrpActorStats::CarryBonus() const { return 2.f * EffectiveRank(1); }
float SrpActorStats::PhysicalDamageMultiplier(bool ranged) const
{
  return 1.f + (ranged ? .004f * EffectiveRank(2)
                       : .005f * EffectiveRank(1));
}
float SrpActorStats::MagicDamageMultiplier() const
{
  return 1.f + .004f * EffectiveRank(3);
}
float SrpActorStats::MagicResistanceFraction() const
{
  return std::clamp(.0025f * EffectiveRank(4), 0.f, .085f);
}
float SrpActorStats::ArmorBonus() const
{
  return .1f * EffectiveRank(0) + .2f * EffectiveRank(5);
}
BaseActorValues SrpActorStats::ComposeMaximums(BaseActorValues base) const
{
  // These are derived afresh; neither serialisation nor replay adds twice.
  // Existing regeneration values and multipliers remain untouched.
  base.health += HealthBonus();
  base.magicka += MagickaBonus();
  base.stamina += StaminaBonus();
  return base;
}
