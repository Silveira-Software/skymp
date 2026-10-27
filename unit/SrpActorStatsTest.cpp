#include "SrpActorStats.h"
#include "TestUtils.hpp"
#include "formulas/TES5DamageFormula.h"
#include <catch2/catch_approx.hpp>
#include <cmath>
PartOne& GetPartOne();

namespace {
nlohmann::json Projection(uint32_t rank = 0)
{
  return { { "schemaVersion", 2 }, { "rulesVersion", "SRP-PROGRESSION-A1" },
           { "revision", 1 }, { "level", 4 }, { "profileId", "1" },
           { "characterId", "srp-test" },
           { "attributes", { { "vigor", rank }, { "strength", rank },
                             { "dexterity", rank }, { "intelligence", rank },
                             { "willpower", rank }, { "constitution", rank } } } };
}
void Bind(MpActor& actor, nlohmann::json p)
{
  p["profileId"] = std::to_string(actor.GetProfileId());
  actor.SetPropertyValueDump("srpActiveCharacterId", "\"srp-test\"", false, false);
  actor.SetPropertyValueDump("srpProgression", p.dump(), false, false);
  actor.ApplySrpActorStats(p);
}
}

TEST_CASE("SRP ranks produce bounded absolute modifiers", "[srp-r36]")
{
  auto p = Projection(10);
  const auto stats = SrpActorStats::FromProjection(p);
  REQUIRE(stats.HealthBonus() == Catch::Approx(24));
  REQUIRE(stats.MagickaBonus() == Catch::Approx(24));
  REQUIRE(stats.StaminaBonus() == Catch::Approx(24));
  REQUIRE(stats.CarryBonus() == Catch::Approx(16));
  REQUIRE(stats.PhysicalDamageMultiplier(false) == Catch::Approx(1.04));
  REQUIRE(stats.PhysicalDamageMultiplier(true) == Catch::Approx(1.032));
  REQUIRE(stats.MagicDamageMultiplier() == Catch::Approx(1.032));
  REQUIRE(stats.MagicResistanceFraction() == Catch::Approx(.02));
  const auto maximum = stats.ComposeMaximums(BaseActorValues{});
  REQUIRE(maximum.health == Catch::Approx(124));
  REQUIRE(maximum.healRate == Catch::Approx(.7));
  REQUIRE(maximum.staminaRateMult == Catch::Approx(100));
  auto capped = SrpActorStats::FromProjection(Projection(200));
  REQUIRE(capped.HealthBonus() <= 100.001f);
  REQUIRE(capped.PhysicalDamageMultiplier(false) < 1.167f);
  REQUIRE(capped.MagicResistanceFraction() < .084f);
}

TEST_CASE("SRP projection rejects forged ranks and invalid identities", "[srp-r36]")
{
  auto p = Projection();
  p["attributes"]["vigor"] = -1;
  REQUIRE_THROWS(SrpActorStats::FromProjection(p));
  p = Projection();p["attributes"]["vigor"] = 201;
  REQUIRE_THROWS(SrpActorStats::FromProjection(p));
  p = Projection();p["attributes"]["vigor"] = 1.5;
  REQUIRE_THROWS(SrpActorStats::FromProjection(p));
  p = Projection();p["revision"] = 0;
  REQUIRE_THROWS(SrpActorStats::FromProjection(p));
  p = Projection();p["characterId"] = "";
  REQUIRE_THROWS(SrpActorStats::FromProjection(p));
  p = Projection();p["bonuses"] = { { "maxHealth", 9999999 } };
  REQUIRE(SrpActorStats::FromProjection(p).HealthBonus() == 0);
}

TEST_CASE("SRP actor change is idempotent and preserves absolute resources", "[srp-r36]")
{
  PartOne& world = GetPartOne();
  world.CreateActor(0xff000000, { 0, 0, 0 }, 0, 0x3c, 1);
  auto& actor = world.worldState.GetFormAt<MpActor>(0xff000000);
  auto values = actor.GetActorValues();
  values.healthPercentage = .5f;values.magickaPercentage = .4f;
  values.healRate = 7.f;values.magickaRateMult = 135.f;
  actor.SetActorValues(values);
  Bind(actor, Projection(10));
  REQUIRE(actor.GetMaximumValues().health == Catch::Approx(124));
  REQUIRE(actor.GetActorValues().healthPercentage * 124 == Catch::Approx(50));
  REQUIRE(actor.GetActorValues().magickaPercentage * 124 == Catch::Approx(40));
  REQUIRE(actor.GetActorValues().healRate == Catch::Approx(7));
  REQUIRE(actor.GetActorValues().magickaRateMult == Catch::Approx(135));
  const auto first = actor.GetChangeForm();
  Bind(actor, Projection(10));
  REQUIRE(actor.GetChangeForm().actorValues.healthPercentage == first.actorValues.healthPercentage);
  REQUIRE(SrpActorStats::Read(actor).revision == 1);
  auto fake = Projection(11);fake["profileId"] = std::to_string(actor.GetProfileId());
  REQUIRE_THROWS(actor.ApplySrpActorStats(fake));
}

TEST_CASE("SRP rollback removes only own bonuses and keeps resources and inventory", "[srp-r36]")
{
  PartOne& world = GetPartOne();
  world.CreateActor(0xff000000, { 0, 0, 0 }, 0, 0x3c, 1);
  auto& actor = world.worldState.GetFormAt<MpActor>(0xff000000);
  Bind(actor, Projection(10));
  const auto inv = actor.GetChangeForm().inv;
  auto p = Projection(10);p["profileId"] = std::to_string(actor.GetProfileId());p["enabled"] = false;
  const float current = actor.GetActorValues().healthPercentage * actor.GetMaximumValues().health;
  actor.ApplySrpActorStats(p);
  REQUIRE(actor.GetMaximumValues().health == Catch::Approx(100));
  REQUIRE(actor.GetActorValues().healthPercentage * 100 == Catch::Approx(current));
  REQUIRE(actor.GetChangeForm().inv == inv);
  actor.ApplySrpActorStats(p);
  REQUIRE(actor.GetMaximumValues().health == Catch::Approx(100));
}

TEST_CASE("SRP saved change form reloads one projection and isolates character switch", "[srp-r36]")
{
  PartOne& world = GetPartOne();
  world.CreateActor(0xff000000, { 0, 0, 0 }, 0, 0x3c, 1);
  auto& original = world.worldState.GetFormAt<MpActor>(0xff000000);
  Bind(original, Projection(10));
  original.DamageActorValue(espm::ActorValue::Health, 20);
  const auto saved = original.GetChangeForm();
  const auto json = MpChangeForm::ToJson(saved).dump();
  simdjson::dom::parser parser;auto document = parser.parse(json);
  auto element = document.value();
  auto decoded = MpChangeForm::JsonToChangeForm(element);
  PartOne& restarted = GetPartOne();
  restarted.CreateActor(0xff000000, { 0, 0, 0 }, 0, 0x3c, 1);
  auto& restored = restarted.worldState.GetFormAt<MpActor>(0xff000000);
  restored.ApplyChangeForm(decoded);
  REQUIRE(restored.GetMaximumValues().health == Catch::Approx(124));
  REQUIRE(restored.GetActorValues().healthPercentage == Catch::Approx(saved.actorValues.healthPercentage));
  Bind(restored, Projection(10));
  REQUIRE(restored.GetMaximumValues().health == Catch::Approx(124));
  restored.SetPropertyValueDump("srpActiveCharacterId", "\"different-character\"", false, false);
  REQUIRE(restored.GetMaximumValues().health == Catch::Approx(100));
  REQUIRE_FALSE(SrpActorStats::Read(restored).enabled);
}

TEST_CASE("Clearing temporary effects cannot erase SRP maximums or heal damage", "[srp-r36]")
{
  PartOne& world = GetPartOne();
  world.CreateActor(0xff000000, { 0, 0, 0 }, 0, 0x3c, 1);
  auto& actor = world.worldState.GetFormAt<MpActor>(0xff000000);
  Bind(actor, Projection(10));
  actor.DamageActorValue(espm::ActorValue::Health, 20);
  const auto before = actor.GetActorValues();
  actor.RemoveAllMagicEffects();
  REQUIRE(actor.GetActorValues().health == Catch::Approx(124));
  REQUIRE(actor.GetActorValues().magicka == Catch::Approx(124));
  REQUIRE(actor.GetActorValues().stamina == Catch::Approx(124));
  REQUIRE(actor.GetActorValues().healthPercentage == before.healthPercentage);
  REQUIRE(actor.GetMaximumValues().health == Catch::Approx(124));
}
