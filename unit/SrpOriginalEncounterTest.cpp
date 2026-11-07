#include "SrpOriginalEncounter.h"
#include "SrpOriginalEncounterEngineFixtures.h"
#include "SrpOriginalReference.h"
#include "SrpOriginalLifecycle.h"
#include "TestUtils.hpp"
#include "database_drivers/FileDatabase.h"
#include "save_storages/AsyncSaveStorage.h"
#include <filesystem>
#include <limits>
#include <unistd.h>
#include <sodium.h>

PartOne& GetPartOne();
namespace {
constexpr uint32_t cowId=0x10ebaf,playerId=0xff000091;
void Storage(PartOne& w)
{
  const auto path=std::filesystem::temp_directory_path()/("srp-encounter-"+std::to_string(getpid()));
  using Save=Viet::AsyncSaveStorage<MpChangeForm,FormDesc,std::vector<FormDesc>>;
  w.worldState.AttachSaveStorage(std::make_shared<Save>(std::make_shared<FileDatabase>(path.string(),std::make_shared<spdlog::logger>("encounter"),true)));
}
void Saved(PartOne& w,MpActor& actor)
{
  const auto g=w.worldState.GetChangeFormGeneration(actor.GetFormId());
  for(int i=0;i<2200&&!w.worldState.IsChangeFormSaved(actor.GetFormId(),g);++i){w.Tick();std::this_thread::sleep_for(std::chrono::milliseconds(5));}
  REQUIRE(w.worldState.IsChangeFormSaved(actor.GetFormId(),g));
}
MpActor& Original(PartOne& w)
{
  w.worldState.npcEnabled=true;
  auto& cow=w.worldState.GetFormAt<MpActor>(cowId);w.worldState.npcEnabled=false;
  SrpOriginalReference::Adopt(cow,{{"speciesId","encounter-cow"},{"policyRevision","context-1"}});Saved(w,cow);
  return cow;
}
nlohmann::json Projection(uint32_t level,const std::string& character="encounter-PC")
{
  return {{"schemaVersion",2},{"rulesVersion","SRP-PROGRESSION-A1"},{"revision",1},
    {"profileId","9191"},{"characterId",character},{"level",level},
    {"attributes",{{"vigor",0},{"strength",0},{"dexterity",0},{"intelligence",0},{"willpower",0},{"constitution",0}}}};
}
std::string Checksum(nlohmann::json value)
{
  value.erase("checksum");const auto text=value.dump();unsigned char bytes[crypto_hash_sha256_BYTES];
  crypto_hash_sha256(bytes,reinterpret_cast<const unsigned char*>(text.data()),text.size());
  constexpr char hex[]="0123456789abcdef";std::string out;for(auto b:bytes){out+=hex[b>>4];out+=hex[b&15];}return out;
}
MpActor& Entrant(PartOne& w,const MpActor& target)
{
  w.CreateActor(playerId,target.GetPos(),0,target.GetCellOrWorld().ToFormId(w.worldState.espmFiles),9191);
  auto& player=w.worldState.GetFormAt<MpActor>(playerId);
  player.SetPropertyValueDump("srpActiveCharacterId","\"encounter-PC\"",false,false);
  player.SetPropertyValueDump("srpProgression",Projection(80).dump(),false,false);
  return player;
}
}

TEST_CASE("Encounter zone level matches captured AE minimum scaling flags and maximum", "[srp-original-encounter]")
{
  for(const auto& f:encounterEngineFixtures)
    REQUIRE(SrpOriginalEncounter::ZoneLevel(f.pc,f.minimum,f.maximum,f.flags,f.scaling)==f.expected);
  REQUIRE_THROWS(SrpOriginalEncounter::ZoneLevel(0,0,0,0,.6f));
  REQUIRE_THROWS(SrpOriginalEncounter::ZoneLevel(3,20,10,0,.6f));
  REQUIRE_THROWS(SrpOriginalEncounter::ZoneLevel(3,0,0,8,.6f));
  REQUIRE_THROWS(SrpOriginalEncounter::ZoneLevel(3,0,0,0,std::numeric_limits<float>::quiet_NaN()));
}

TEST_CASE("Encounter capture freezes canonical owner level and persists before use", "[srp-original-encounter]")
{
  auto& w=GetPartOne();Storage(w);auto& cow=Original(w);auto& pc=Entrant(w,cow);
  const auto inventory=cow.GetInventory().ToJson();const auto values=cow.GetActorValues().ToTuple();
  const auto result=SrpOriginalEncounter::Capture(cow,pc);
  REQUIRE(result.at("duplicate")==false);REQUIRE(result.at("saved")==false);
  REQUIRE(result.at("receipt").at("zoneContext").at("pc").at("level")==80);
  REQUIRE_THROWS_WITH(SrpOriginalEncounter::Read(cow,true),"SRP_ENCOUNTER_SAVE_PENDING");
  Saved(w,cow);const auto receipt=*SrpOriginalEncounter::Read(cow,true);
  REQUIRE(cow.GetInventory().ToJson()==inventory);REQUIRE(cow.GetActorValues().ToTuple()==values);
  const auto generation=w.worldState.GetChangeFormGeneration(cowId);
  pc.SetPropertyValueDump("srpActiveCharacterId","\"replacement-PC\"",false,false);
  pc.SetPropertyValueDump("srpProgression",Projection(3,"replacement-PC").dump(),false,false);
  REQUIRE(SrpOriginalEncounter::Capture(cow,pc).at("receipt")==receipt);
  REQUIRE(w.worldState.GetChangeFormGeneration(cowId)==generation);
  cow.SetPropertyValueDump("unrelatedOfflineField","1",false,false);
  REQUIRE(*SrpOriginalEncounter::Read(cow,true)==receipt);
  REQUIRE(SrpOriginalLifecycle::Inspect(cow).at("xpEnabled")==false);
}

TEST_CASE("Encounter rejects unbound canonical character and distant entrant without writing", "[srp-original-encounter]")
{
  auto& w=GetPartOne();Storage(w);auto& cow=Original(w);auto& pc=Entrant(w,cow);
  const auto generation=w.worldState.GetChangeFormGeneration(cowId);
  pc.SetPropertyValueDump("srpActiveCharacterId","\"wrong-owner\"",false,false);
  REQUIRE_THROWS_WITH(SrpOriginalEncounter::Capture(cow,pc),"SRP_ENCOUNTER_CANONICAL_OWNER_MISMATCH");
  REQUIRE_FALSE(SrpOriginalEncounter::Read(cow).has_value());
  REQUIRE(w.worldState.GetChangeFormGeneration(cowId)==generation);
  pc.SetPropertyValueDump("srpActiveCharacterId","\"encounter-PC\"",false,false);
  pc.SetPos({cow.GetPos().x+10000,cow.GetPos().y,cow.GetPos().z});
  REQUIRE_THROWS_WITH(SrpOriginalEncounter::Capture(cow,pc),"SRP_ENCOUNTER_ENTRANT_TOO_FAR");
  REQUIRE(w.worldState.GetChangeFormGeneration(cowId)==generation);
}

TEST_CASE("Encounter cold database index recovers without materializing the original", "[srp-original-encounter]")
{
  auto& w=GetPartOne();Storage(w);auto& cow=Original(w);auto& pc=Entrant(w,cow);
  const auto receipt=SrpOriginalEncounter::Capture(cow,pc).at("receipt");Saved(w,cow);
  const auto image=cow.GetChangeForm();
  auto& restored=GetPartOne();restored.worldState.LoadChangeForm(image,FormCallbacks());
  REQUIRE_FALSE(restored.worldState.LookupFormByIdNoLoad(cowId));
  const auto images=restored.worldState.GetSrpOriginalEncounterImages();
  REQUIRE(images.size()==1);REQUIRE(images[0].at("receipt")==receipt);REQUIRE(images[0].at("durable")==true);
  REQUIRE_FALSE(restored.worldState.LookupFormByIdNoLoad(cowId));
}

TEST_CASE("Encounter corrupt receipt cannot be reused or repaired by retry", "[srp-original-encounter]")
{
  auto& w=GetPartOne();Storage(w);auto& cow=Original(w);auto& pc=Entrant(w,cow);
  auto receipt=SrpOriginalEncounter::Capture(cow,pc).at("receipt");Saved(w,cow);
  receipt["finalLevel"]=900;cow.SetPropertyValueDump(SrpOriginalEncounter::kProperty,receipt.dump(),false,false);
  REQUIRE_THROWS(SrpOriginalEncounter::Read(cow));REQUIRE_THROWS(SrpOriginalEncounter::Capture(cow,pc));
  REQUIRE(cow.GetDynamicFields().GetAsJson().at(SrpOriginalEncounter::kProperty)==receipt);
}

TEST_CASE("Encounter conflicting stored and loaded zone histories block confirmation", "[srp-original-encounter]")
{
  auto& w=GetPartOne();Storage(w);auto& cow=Original(w);auto& pc=Entrant(w,cow);
  SrpOriginalEncounter::Capture(cow,pc);Saved(w,cow);const auto image=cow.GetChangeForm();
  auto& restored=GetPartOne();restored.worldState.LoadChangeForm(image,FormCallbacks());
  restored.worldState.npcEnabled=true;auto& loaded=restored.worldState.GetFormAt<MpActor>(cowId);
  auto fork=*SrpOriginalEncounter::Read(loaded);fork["zoneContext"]["pc"]["characterId"]="forked-capture";
  fork["zoneContext"]["checksum"]=Checksum(fork["zoneContext"]);fork["checksum"]=Checksum(fork);
  loaded.SetPropertyValueDump(SrpOriginalEncounter::kProperty,fork.dump(),false,false);
  REQUIRE_THROWS_WITH(SrpOriginalEncounter::Read(loaded,true),"SRP_ENCOUNTER_ZONE_CONFLICT");
  REQUIRE(loaded.GetDynamicFields().GetAsJson().at(SrpOriginalEncounter::kProperty)==fork);
}

TEST_CASE("Encounter cold image cannot borrow a player's character binding", "[srp-original-encounter]")
{
  auto& w=GetPartOne();Storage(w);auto& cow=Original(w);auto& pc=Entrant(w,cow);
  SrpOriginalEncounter::Capture(cow,pc);Saved(w,cow);auto image=cow.GetChangeForm();
  image.dynamicFields.SetValueDump("srpActiveCharacterId","\"forbidden-binding\"");
  auto& restored=GetPartOne();restored.worldState.LoadChangeForm(image,FormCallbacks());
  REQUIRE_THROWS_WITH(restored.worldState.GetSrpOriginalEncounterImages(),"SRP_ENCOUNTER_ORIGINAL_IMAGE_CORRUPT");
  REQUIRE_FALSE(restored.worldState.LookupFormByIdNoLoad(cowId));
}
