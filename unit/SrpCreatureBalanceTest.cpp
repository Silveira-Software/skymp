#include "TestUtils.hpp"
#include "CreateActorMessage.h"
#include "HitMessage.h"
#include "SpellCastData.h"
#include "formulas/DamageMultFormula.h"
#include "formulas/TES5DamageFormula.h"
#include "libespm/RACE.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_all.hpp>

PartOne& GetPartOne();

namespace {
constexpr uint32_t kBear = 0x23a8b;
constexpr uint32_t kBearRef = 0xff000001;
constexpr uint32_t kPlayerRef = 0xff000002;

nlohmann::json Rules()
{
  return {{"schemaVersion", 1}, {"profiles", nlohmann::json::array({
    {{"base", "23a8b:Skyrim.esm"}, {"level", 12}, {"healthCap", 200},
     {"physicalHitCap", 10}, {"magicHitCap", 10}}
  })}};
}

MpActor& CreateBear(PartOne& server)
{
  auto& world = server.worldState;
  world.AddForm(std::make_unique<MpActor>(LocationalData{{}, {}, FormDesc::FromString("3c:Skyrim.esm")},
    server.CreateFormCallbacks(), kBear), kBearRef);
  return world.GetFormAt<MpActor>(kBearRef);
}
}

TEST_CASE("Creature balance caps actual shared HP and damage and preserves a wounded reload", "[srp-creature-balance]")
{
  auto& server = GetPartOne();
  server.SetDamageFormula(std::make_unique<TES5DamageFormula>());
  auto& world = server.worldState;
  auto& bear = CreateBear(server);
  server.CreateActor(kPlayerRef, {}, 0, 0x3c, 91);
  auto& player = world.GetFormAt<MpActor>(kPlayerRef);

  const float playerMax = player.GetMaximumValues().health;
  const float originalMaximum = bear.GetMaximumValues().health;
  REQUIRE(originalMaximum > 200.f);
  HitData hit;
  hit.source = 0x1f4;
  const auto originalDamage = server.CalculateDamage(bear, player, hit);
  REQUIRE(originalDamage > 10.f);
  const auto unarmed = espm::GetData<espm::RACE>(bear.GetRaceId(), &world).unarmedDamage;
  REQUIRE(unarmed > 10.f);
  const float playerDamage = server.CalculateDamage(player, bear, hit);
  SpellCastData spell{kBearRef, kPlayerRef, 0x12fd0, false, false, SpellType::Left};
  REQUIRE(server.CalculateDamage(bear, player, spell) > 10.f);

  world.ConfigureSrpCreatureBalance(Rules());
  REQUIRE(world.GetSrpCreatureBalance(bear)->level == 12);
  REQUIRE(bear.GetMaximumValues().health == 200.f);
  const auto cappedDamage = server.CalculateDamage(bear, player, hit);
  REQUIRE(cappedDamage == Catch::Approx(originalDamage * 10.f / unarmed));
  REQUIRE(server.CalculateDamage(bear, player, spell) == 10.f);
  REQUIRE(server.CalculateDamage(player, bear, hit) == playerDamage);
  REQUIRE(player.GetMaximumValues().health == playerMax);
  DamageMultFormula npcMultiplier(std::make_unique<TES5DamageFormula>(), {{"multiplier", 2.f}});
  REQUIRE(npcMultiplier.CalculateDamage(bear, player, hit) == Catch::Approx(cappedDamage));
  REQUIRE(npcMultiplier.CalculateDamage(bear, player, spell) == 10.f);
  hit.isHitBlocked = true;
  REQUIRE(server.CalculateDamage(bear, player, hit) == Catch::Approx(cappedDamage * .1f));
  hit.isHitBlocked = false;
  const nlohmann::json projection = {{"schemaVersion", 2}, {"rulesVersion", "SRP-PROGRESSION-A1"},
    {"revision", 1}, {"level", 4}, {"profileId", "91"}, {"characterId", "c1"},
    {"attributes", {{"vigor", 10}, {"strength", 0}, {"dexterity", 0},
      {"intelligence", 0}, {"willpower", 0}, {"constitution", 10}}}};
  player.SetPropertyValueDump("srpActiveCharacterId", "\"c1\"", false, false);
  player.SetPropertyValueDump("srpProgression", projection.dump(), false, false);
  player.ApplySrpActorStats(projection);
  REQUIRE(server.CalculateDamage(bear, player, hit) < cappedDamage);

  auto saved = bear.GetChangeForm();
  saved.actorValues.healthPercentage = .25f;
  bear.ApplyChangeForm(saved);
  REQUIRE(bear.GetActorValues().healthPercentage == .25f);
  REQUIRE(bear.GetMaximumValues().health * bear.GetActorValues().healthPercentage == 50.f);
  CreateActorMessage message;
  bear.VisitProperties(message, VisitPropertiesMode::All);
  REQUIRE(message.props.health == 200.f);
  REQUIRE(message.props.healthPercentage == .25f);

  auto legacy = saved;
  legacy.dynamicFields.SetValueDump("private.srpCreatureBalanceV1", "null");
  bear.ApplyChangeForm(legacy);
  REQUIRE(bear.GetMaximumValues().health * bear.GetActorValues().healthPercentage == originalMaximum * .25f);
  const auto migrated = bear.GetChangeForm();
  bear.ApplyChangeForm(migrated);
  REQUIRE(bear.GetMaximumValues().health * bear.GetActorValues().healthPercentage == originalMaximum * .25f);

  world.ConfigureSrpCreatureBalance({{"schemaVersion", 1}, {"profiles", nlohmann::json::array()}});
  bear.ApplyChangeForm(migrated);
  REQUIRE(bear.GetMaximumValues().health * bear.GetActorValues().healthPercentage == originalMaximum * .25f);
  world.ConfigureSrpCreatureBalance(Rules());

  bear.SetPropertyValueDump("srpMountOwner", "91", false, false);
  REQUIRE(world.GetSrpCreatureBalance(bear) == nullptr);
  REQUIRE(bear.GetMaximumValues().health == originalMaximum);
}

TEST_CASE("Creature balance fails closed on invalid records and leaves unsanctioned actors alone", "[srp-creature-balance]")
{
  auto& server = GetPartOne();
  auto& world = server.worldState;
  auto bad = Rules();
  bad["profiles"][0]["base"] = "1397e:Skyrim.esm";
  REQUIRE_THROWS(world.ConfigureSrpCreatureBalance(bad));
  bad = Rules();
  bad["profiles"][0]["healthCap"] = 0;
  REQUIRE_THROWS(world.ConfigureSrpCreatureBalance(bad));
  bad = Rules();
  const auto row = bad["profiles"][0];
  bad["profiles"].push_back(row);
  REQUIRE_THROWS(world.ConfigureSrpCreatureBalance(bad));
  auto& bear = CreateBear(server);
  const auto originalMaximum = bear.GetMaximumValues().health;
  world.ConfigureSrpCreatureBalance(Rules());
  bear.SetPropertyValueDump("srpSpawnSource", "\"ADMIN\"", false, false);
  REQUIRE(world.GetSrpCreatureBalance(bear) == nullptr);
  REQUIRE(bear.GetMaximumValues().health == originalMaximum);
  REQUIRE_THROWS(world.ConfigureSrpCreatureBalance(bad));
  bear.SetPropertyValueDump("srpSpawnSource", "\"FAUNA\"", false, false);
  REQUIRE(world.GetSrpCreatureBalance(bear) != nullptr);
}

TEST_CASE("Creature balance waits for native template selection before first save", "[srp-creature-balance]")
{
  auto& server = GetPartOne();
  auto& world = server.worldState;
  auto rules = Rules();
  rules["profiles"][0]["base"] = "100767:Skyrim.esm";
  rules["profiles"][0]["level"] = 50;
  world.ConfigureSrpCreatureBalance(rules);
  // Init requests a save before EnsureTemplateChainEvaluated. That request
  // must not resolve HP from an empty chain or poison NPC admission.
  world.AddForm(std::make_unique<MpActor>(LocationalData{{}, {}, FormDesc::FromString("3c:Skyrim.esm")},
    server.CreateFormCallbacks(), 0x100767), kBearRef);
  auto& npc = world.GetFormAt<MpActor>(kBearRef);
  REQUIRE_FALSE(npc.GetTemplateChain().empty());
  REQUIRE(world.GetSrpCreatureBalance(npc) != nullptr);
  REQUIRE(npc.GetMaximumValues().health <= 200.f);
  REQUIRE(npc.GetChangeForm().dynamicFields.GetAsJson().at("private.srpCreatureBalanceV1").at("health") <= 200);
}
