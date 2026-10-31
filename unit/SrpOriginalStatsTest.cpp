#include "SrpOriginalStats.h"
#include "SrpOriginalReference.h"
#include "SrpOriginalLifecycle.h"
#include "SrpOriginalStatsEngineFixtures.h"
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
  const auto balanced=SrpOriginalStats::Allocate(base,10,{3,0,3},5,10);
  REQUIRE(balanced.health==190);
  REQUIRE(balanced.magicka==50);
  REQUIRE(balanced.stamina==95);
  const auto mage=SrpOriginalStats::Allocate(base,11,{1,4,0},5,10);
  REQUIRE(mage.health==170);
  REQUIRE(mage.magicka==130);
  REQUIRE(mage.stamina==50);
  const auto levelOne=SrpOriginalStats::Allocate(base,1,{1,1,1},5,10);
  REQUIRE(levelOne.health==base.health);
  REQUIRE(levelOne.magicka==base.magicka);
  REQUIRE(levelOne.stamina==base.stamina);
  base.health=-5;
  REQUIRE(SrpOriginalStats::Allocate(base,10,{1,0,0},5,10).health==130);
}

TEST_CASE("Original resource distribution matches captured AE instructions", "[srp-original-stats]")
{
  BaseActorValues base;base.health=100;base.magicka=50;base.stamina=50;
  for(const auto& f:OriginalEngineFixtures::allocations) {
    INFO("level="<<f.level<<" weights="<<int(f.weights[0])<<","<<int(f.weights[1])<<","<<int(f.weights[2])<<" budget="<<f.budget);
    const auto actual=SrpOriginalStats::Allocate(base,f.level,f.weights,0,f.budget);
    REQUIRE(actual.health==100+f.growth[0]);
    REQUIRE(actual.magicka==50+f.growth[1]);
    REQUIRE(actual.stamina==50+f.growth[2]);
  }
  const auto zero=SrpOriginalStats::Allocate(base,10,{0,0,0},5,10);
  REQUIRE(zero.health==145);REQUIRE(zero.magicka==50);REQUIRE(zero.stamina==50);
  base.health=100.75f;base.magicka=50.9f;base.stamina=50.5f;
  const auto truncated=SrpOriginalStats::Allocate(base,2,{0,0,0},5.9f,10,{1,-50,0});
  REQUIRE(truncated.health==106);
  REQUIRE(truncated.magicka==0);REQUIRE(truncated.stamina==50);
  REQUIRE_THROWS(SrpOriginalStats::Allocate(base,2,{0,0,0},5.9f,10,{1,-51,0}));
}

TEST_CASE("Original stat composer rejects invalid authored inputs and signed cache overflow", "[srp-original-stats]")
{
  BaseActorValues base;
  REQUIRE_THROWS(SrpOriginalStats::Allocate(base,0,{1,0,0},5,10));
  REQUIRE_THROWS(SrpOriginalStats::Allocate(base,1001,{1,0,0},5,10));
  REQUIRE_THROWS(SrpOriginalStats::Allocate(base,10,{1,0,0},5,1001));
  REQUIRE_THROWS(SrpOriginalStats::Allocate(base,10,{1,0,0},std::numeric_limits<float>::quiet_NaN(),10));
  REQUIRE_THROWS_WITH(SrpOriginalStats::Allocate(base,1000,{1,0,0},1000,10),"SRP_ORIGINAL_STATS_ENGINE_CACHE_OVERFLOW");
  base.magicka=-200;
  REQUIRE_THROWS(SrpOriginalStats::Allocate(base,10,{1,0,0},5,10));
}

TEST_CASE("Original cache initialization matches separate AE truncations", "[srp-original-stats]")
{
  for (const auto& f:OriginalEngineFixtures::initials) {
    BaseActorValues base;base.health=f.race[0];base.magicka=f.race[1];base.stamina=f.race[2];
    if(f.expected[0]<=0 || f.expected[1]<0 || f.expected[2]<=0) {
      REQUIRE_THROWS(SrpOriginalStats::Allocate(base,f.level,{0,0,0},f.bonus,10,f.offsets));
    } else {
      const auto values=SrpOriginalStats::Allocate(base,f.level,{0,0,0},f.bonus,10,f.offsets);
      REQUIRE(values.health==f.expected[0]);REQUIRE(values.magicka==f.expected[1]);REQUIRE(values.stamina==f.expected[2]);
    }
  }
}

TEST_CASE("PC multiplier matches AE instruction results for every resolved catalog stats source", "[srp-original-stats]")
{
  for(const auto& f:OriginalEngineFixtures::levels) {
    espm::NPC_::Data data;data.hasStatsConfiguration=true;data.pcLevelMult=f.pc;
    data.level=f.value;data.calcMinLevel=f.minimum;data.calcMaxLevel=f.maximum;
    if (f.expected==0) {
      // The source catalog also contains authored zero-level corpse bases.
      // Never admit them as living actors merely to match an engine value.
      REQUIRE_THROWS(SrpOriginalStats::ResolveLevel(data,f.pcLevel));
    } else REQUIRE(SrpOriginalStats::ResolveLevel(data,f.pcLevel)==f.expected);
  }
  espm::NPC_::Data data;data.hasStatsConfiguration=true;data.pcLevelMult=true;data.level=65535;
  REQUIRE(SrpOriginalStats::ResolveLevel(data,1)==65);
  REQUIRE_THROWS_WITH(SrpOriginalStats::ResolveLevel(data,std::nullopt),"SRP_ORIGINAL_STATS_ENCOUNTER_LEVEL_REQUIRED");
  REQUIRE_THROWS(SrpOriginalStats::ResolveLevel(data,0));
  REQUIRE_THROWS(SrpOriginalStats::ResolveLevel(data,1001));
  data.level=1000;data.calcMinLevel=80;data.calcMaxLevel=20;
  REQUIRE_THROWS(SrpOriginalStats::ResolveLevel(data,3));
}

TEST_CASE("NPC ACBS reads unsigned multiplier from unaligned plugin bytes", "[srp-original-stats]")
{
  std::array<char,55> bytes{};auto* raw=bytes.data()+1;
  auto word=[](char* dest,uint32_t value){std::memcpy(dest,&value,4);};
  std::memcpy(raw,"NPC_",4);word(raw+4,30);word(raw+12,0x01020304);
  std::memcpy(raw+24,"ACBS",4);const uint16_t size=24;
  std::memcpy(raw+28,&size,2);word(raw+30,0x90);
  const uint16_t multiplier=65535,minimum=2,maximum=80;
  std::memcpy(raw+38,&multiplier,2);std::memcpy(raw+40,&minimum,2);std::memcpy(raw+42,&maximum,2);
  espm::Browser browser(raw,54);espm::CompressedFieldsCache cache;
  const auto* record=espm::Convert<espm::NPC_>(browser.LookupById(0x01020304));
  REQUIRE(record);const auto data=record->GetData(cache);
  REQUIRE(data.hasStatsConfiguration);REQUIRE(data.autoCalcStats);REQUIRE(data.pcLevelMult);
  REQUIRE(data.level==65535);REQUIRE(data.calcMinLevel==2);REQUIRE(data.calcMaxLevel==80);
  REQUIRE(SrpOriginalStats::ResolveLevel(data,1)==65);
}

TEST_CASE("Legacy original stats receipts only permit equivalent ten-point retry", "[srp-original-stats]")
{
  nlohmann::json old={{"schemaVersion",1},{"identity",{{"instanceId","original-A"}}},
    {"maximums",{{"health",59},{"magicka",256},{"stamina",70}}},{"checksum","old"}};
  auto proposed=old;
  proposed["resourceLevelUp"]=10;proposed["resourceLevelUpSource"]="canonical:Skyrim.esm";
  proposed["checksum"]="new";
  REQUIRE(SrpOriginalStats::SameSnapshot(old,proposed));
  REQUIRE(old.at("checksum")=="old");
  proposed["resourceLevelUp"]=7;
  REQUIRE_FALSE(SrpOriginalStats::SameSnapshot(old,proposed));
  proposed["resourceLevelUp"]=10;proposed["maximums"]["health"]=60;
  REQUIRE_FALSE(SrpOriginalStats::SameSnapshot(old,proposed));
  proposed["maximums"]["health"]=59;proposed["identity"]["instanceId"]="original-B";
  REQUIRE_FALSE(SrpOriginalStats::SameSnapshot(old,proposed));
  proposed["identity"]["instanceId"]="original-A";old["resourceLevelUp"]=10;
  REQUIRE_FALSE(SrpOriginalStats::SameSnapshot(old,proposed));
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
