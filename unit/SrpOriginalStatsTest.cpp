#include "SrpOriginalStats.h"
#include "SrpOriginalReference.h"
#include "SrpOriginalLifecycle.h"
#include "TestUtils.hpp"
#include "database_drivers/FileDatabase.h"
#include "save_storages/AsyncSaveStorage.h"
#include "libespm/RecordHeaderAccess.h"
#include <catch2/catch_approx.hpp>
#include <filesystem>
#include <limits>
#include <unistd.h>

PartOne& GetPartOne();
namespace {
constexpr uint32_t cowId=0x10ebaf;
void Storage(PartOne& w)
{
  auto path=std::filesystem::temp_directory_path()/("srp-original-stats-"+std::to_string(getpid()));
  using Save=Viet::AsyncSaveStorage<MpChangeForm,FormDesc,std::vector<FormDesc>>;
  auto db=std::make_shared<FileDatabase>(path.string(),std::make_shared<spdlog::logger>("original-stats"),true);
  w.worldState.AttachSaveStorage(std::make_shared<Save>(db));
}
void Saved(PartOne& w,MpActor& a)
{
  auto g=w.worldState.GetChangeFormGeneration(a.GetFormId());
  for(int i=0;i<2200&&!w.worldState.IsChangeFormSaved(a.GetFormId(),g);++i){w.Tick();std::this_thread::sleep_for(std::chrono::milliseconds(5));}
  REQUIRE(w.worldState.IsChangeFormSaved(a.GetFormId(),g));
}
}

TEST_CASE("Original fixed-level class growth conserves the authored budget", "[srp-original-stats]")
{
  BaseActorValues base;base.health=100;base.magicka=50;base.stamina=50;
  const auto balanced=SrpOriginalStats::Allocate(base,10,{3,0,3},5);
  REQUIRE(balanced.health==190);
  REQUIRE(balanced.magicka==50);
  REQUIRE(balanced.stamina==95);
  const auto mage=SrpOriginalStats::Allocate(base,11,{1,4,0},5);
  REQUIRE(mage.health==170);
  REQUIRE(mage.magicka==130);
  REQUIRE(mage.stamina==50);
  const auto levelOne=SrpOriginalStats::Allocate(base,1,{1,1,1},5);
  REQUIRE(levelOne.health==base.health);
  REQUIRE(levelOne.magicka==base.magicka);
  REQUIRE(levelOne.stamina==base.stamina);
  base.health=-5;
  REQUIRE(SrpOriginalStats::Allocate(base,10,{1,0,0},5).health==130);
}

TEST_CASE("Original stat composer rejects unknown rounding and invalid authored inputs", "[srp-original-stats]")
{
  BaseActorValues base;
  REQUIRE_THROWS_WITH(SrpOriginalStats::Allocate(base,2,{1,1,1},5),"SRP_ORIGINAL_STATS_ENGINE_ROUNDING_UNVERIFIED");
  REQUIRE_THROWS(SrpOriginalStats::Allocate(base,0,{1,0,0},5));
  REQUIRE_THROWS(SrpOriginalStats::Allocate(base,1001,{1,0,0},5));
  REQUIRE_THROWS(SrpOriginalStats::Allocate(base,10,{0,0,0},5));
  REQUIRE_THROWS(SrpOriginalStats::Allocate(base,10,{1,0,0},std::numeric_limits<float>::quiet_NaN()));
  base.magicka=-200;
  REQUIRE_THROWS(SrpOriginalStats::Allocate(base,10,{1,0,0},5));
}

TEST_CASE("Original NPC parser reads actual ACBS and class without changing legacy player values", "[srp-original-stats]")
{
  auto& w=GetPartOne();
  const auto records=w.worldState.GetEspm().GetBrowser().GetDistinctRecordsByType("NPC_");
  size_t configured=0,scaled=0,fixed=0;
  for(const auto& record:records){
    const auto data=reinterpret_cast<const espm::NPC_*>(record.rec)->GetData(w.worldState.GetEspmCache());
    if(!data.hasStatsConfiguration)continue;
    ++configured;
    if(data.pcLevelMult)++scaled;else if(data.level>0)++fixed;
  }
  REQUIRE(configured>100);REQUIRE(scaled>0);REQUIRE(fixed>0);
  w.CreateActor(0xff000001,{0,0,0},0,0x3c,9191);
  auto& player=w.worldState.GetFormAt<MpActor>(0xff000001);
  const auto before=player.GetChangeForm();
  REQUIRE_THROWS(player.ApplySrpOriginalStats());
  REQUIRE(player.GetChangeForm().actorValues.ToTuple()==before.actorValues.ToTuple());
  REQUIRE(player.GetChangeForm().dynamicFields==before.dynamicFields);
  REQUIRE(player.GetChangeForm().inv.ToJson()==before.inv.ToJson());
}

TEST_CASE("Original stats commit is idempotent and preserves absolute H M S after reload", "[srp-original-stats]")
{
  auto& w=GetPartOne();Storage(w);w.worldState.npcEnabled=true;
  auto& actor=w.worldState.GetFormAt<MpActor>(cowId);w.worldState.npcEnabled=false;
  SrpOriginalReference::Adopt(actor,{{"speciesId","offline-cow"},{"policyRevision","stats-test"}});Saved(w,actor);
  auto values=actor.GetActorValues();
  values.healthPercentage=.37f;values.magickaPercentage=.23f;values.staminaPercentage=.61f;
  actor.SetActorValues(values);Saved(w,actor);
  const auto before=actor.GetMaximumValues();const auto inv=actor.GetInventory().ToJson();
  const auto result=actor.ApplySrpOriginalStats();
  REQUIRE(result.at("duplicate")==false);
  const auto maximum=actor.GetMaximumValues();const auto after=actor.GetActorValues();
  REQUIRE(after.healthPercentage*maximum.health==Catch::Approx(values.healthPercentage*before.health));
  REQUIRE(after.magickaPercentage*maximum.magicka==Catch::Approx(values.magickaPercentage*before.magicka));
  REQUIRE(after.staminaPercentage*maximum.stamina==Catch::Approx(values.staminaPercentage*before.stamina));
  Saved(w,actor);
  const auto generation=w.worldState.GetChangeFormGeneration(cowId);
  REQUIRE(actor.ApplySrpOriginalStats().at("duplicate")==true);
  REQUIRE(w.worldState.GetChangeFormGeneration(cowId)==generation);
  const auto form=actor.GetChangeForm();
  REQUIRE(form.dynamicFields.GetAsJson().at("private.srpResourceMaximumsV1").at("schemaVersion")==1);
  actor.ApplyChangeForm(form);
  REQUIRE(actor.GetActorValues().healthPercentage==Catch::Approx(after.healthPercentage));
  REQUIRE(actor.GetActorValues().magickaPercentage==Catch::Approx(after.magickaPercentage));
  REQUIRE(actor.GetActorValues().staminaPercentage==Catch::Approx(after.staminaPercentage));
  REQUIRE(actor.GetInventory().ToJson()==inv);
  REQUIRE(SrpOriginalLifecycle::Inspect(actor).at("xpEnabled")==false);
  auto corrupt=form.dynamicFields.GetAsJson().at(SrpOriginalStats::kProperty);
  corrupt["maximums"]["health"]=maximum.health+1;
  actor.SetPropertyValueDump(SrpOriginalStats::kProperty,corrupt.dump(),false,false);
  REQUIRE_THROWS_WITH(actor.GetMaximumValues(),"SRP_ORIGINAL_STATS_SNAPSHOT_CORRUPT");
  REQUIRE_THROWS(actor.ApplySrpOriginalStats());
  REQUIRE(actor.GetDynamicFields().GetAsJson().at(SrpOriginalStats::kProperty)==corrupt);
  REQUIRE(actor.GetInventory().ToJson()==inv);
}
