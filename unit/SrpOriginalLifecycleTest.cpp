#include "SrpOriginalLifecycle.h"
#include "SrpOriginalReference.h"
#include "TestUtils.hpp"
#include "database_drivers/FileDatabase.h"
#include "save_storages/AsyncSaveStorage.h"
#include <filesystem>
#include <unistd.h>
PartOne& GetPartOne();
namespace {
constexpr uint32_t cowId=0x10ebaf; // original fixture in NpcExists.cpp
MpActor& Original(PartOne& w)
{
  w.worldState.npcEnabled=true;
  auto& a=w.worldState.GetFormAt<MpActor>(cowId);
  w.worldState.npcEnabled=false;
  SrpOriginalReference::Adopt(a,{{"speciesId","offline-cow"},{"policyRevision","R382-observation"}});
  return a;
}
void AttachStorage(PartOne& w,const std::string& suffix)
{
  auto dir=std::filesystem::temp_directory_path()/("srp-original-life-"+std::to_string(getpid())+suffix);
  using Storage=Viet::AsyncSaveStorage<MpChangeForm,FormDesc,std::vector<FormDesc>>;
  auto db=std::make_shared<FileDatabase>(dir.string(),std::make_shared<spdlog::logger>("original-life"),true);
  w.worldState.AttachSaveStorage(std::make_shared<Storage>(db));
}
void Saved(PartOne& w,MpActor& a)
{
  auto g=w.worldState.GetChangeFormGeneration(a.GetFormId());
  for(int i=0;i<2200&&!w.worldState.IsChangeFormSaved(a.GetFormId(),g);++i){w.Tick();std::this_thread::sleep_for(std::chrono::milliseconds(5));}
  REQUIRE(w.worldState.IsChangeFormSaved(a.GetFormId(),g));
}
}
TEST_CASE("R38.2 two killers cannot materialize native drops or death receipt twice", "[srp-r382]")
{
  auto& w=GetPartOne();auto& cow=Original(w);
  for(int i=0;i<2;++i){w.CreateActor(0xff000001+i,cow.GetPos(),0,0x3c,101+i);auto& a=w.worldState.GetFormAt<MpActor>(0xff000001+i);a.SetPropertyValueDump("srpActiveCharacterId",nlohmann::json("killer-"+std::to_string(i)).dump(),false,false);}
  auto& first=w.worldState.GetFormAt<MpActor>(0xff000001);
  auto& second=w.worldState.GetFormAt<MpActor>(0xff000002);
  cow.Kill(&first,false);REQUIRE(cow.IsDead());
  auto receipt=SrpOriginalLifecycle::Inspect(cow),inventory=cow.GetInventory().ToJson();
  auto generation=w.worldState.GetChangeFormGeneration(cowId);
  cow.Kill(&second,true);cow.SetIsDead(true);
  REQUIRE(cow.GetInventory().ToJson()==inventory);
  REQUIRE(w.worldState.GetChangeFormGeneration(cowId)==generation);
  REQUIRE(SrpOriginalLifecycle::Inspect(cow).at("lifecycle")==receipt.at("lifecycle"));
  REQUIRE(receipt.at("lifecycle").at("death").at("killer").at("characterId")=="killer-0");
  REQUIRE(receipt.at("lootEnabled")==false);REQUIRE(receipt.at("xpEnabled")==false);
  const auto a=first.GetInventory().ToJson(),b=second.GetInventory().ToJson();
  REQUIRE_THROWS(cow.TakeItem(first,Inventory::Entry(0xf,1)));
  REQUIRE_THROWS(cow.TakeItem(second,Inventory::Entry(0xf,1)));
  REQUIRE_THROWS(cow.PutItem(first,Inventory::Entry(0xf,1)));
  REQUIRE(first.GetInventory().ToJson()==a);REQUIRE(second.GetInventory().ToJson()==b);
}
TEST_CASE("R38.2 original corpse cannot use default respawn timer or resurrect setter", "[srp-r382]")
{
  auto& w=GetPartOne();auto& cow=Original(w);cow.SetRespawnTime(0);cow.Kill(nullptr,false);
  const auto receipt=SrpOriginalLifecycle::Inspect(cow).at("lifecycle");
  cow.RespawnWithDelay();cow.Respawn();cow.SetIsDead(false);w.Tick();
  REQUIRE(cow.IsDead());REQUIRE_FALSE(cow.IsRespawning());
  REQUIRE(SrpOriginalLifecycle::Inspect(cow).at("lifecycle")==receipt);
}
TEST_CASE("R38.2 original unload requires durability and reload preserves current corpse", "[srp-r382]")
{
  auto& w=GetPartOne();AttachStorage(w,"-unload");auto& cow=Original(w);
  REQUIRE_THROWS(w.worldState.DestroyForm<MpActor>(cowId));
  cow.Kill(nullptr,false);Saved(w,cow);
  const auto cf=cow.GetChangeForm();const auto receipt=SrpOriginalLifecycle::Inspect(cow).at("lifecycle");
  w.worldState.npcEnabled=true;
  w.worldState.DestroyForm<MpActor>(cowId);
  REQUIRE_FALSE(w.worldState.LookupFormByIdNoLoad(cowId));
  auto& loaded=w.worldState.GetFormAt<MpActor>(cowId);
  REQUIRE(loaded.IsDead());REQUIRE_FALSE(loaded.IsRespawning());
  REQUIRE(loaded.GetInventory().ToJson()==cf.inv.ToJson());
  REQUIRE(SrpOriginalLifecycle::Inspect(loaded).at("lifecycle")==receipt);
}
TEST_CASE("R38.2 corrupt death receipt is retained and blocks further native death", "[srp-r382]")
{
  auto& w=GetPartOne();auto& cow=Original(w);cow.Kill(nullptr,false);
  auto state=SrpOriginalLifecycle::Inspect(cow).at("lifecycle");state["death"]["operationId"]="foreign";
  cow.SetPropertyValueDump("private.srpOriginalLifecycleV1",state.dump(),false,false);
  const auto before=cow.GetInventory().ToJson();
  REQUIRE_THROWS(SrpOriginalLifecycle::Inspect(cow));REQUIRE_THROWS(cow.Kill(nullptr,false));
  REQUIRE(cow.GetDynamicFields().GetAsJson().at("private.srpOriginalLifecycleV1")==state);
  REQUIRE(cow.GetInventory().ToJson()==before);
}
TEST_CASE("R38.2 orphan corpse receipt cannot downgrade to legacy loot or resurrection", "[srp-r382]")
{
  auto& w=GetPartOne();auto& cow=Original(w);cow.Kill(nullptr,false);
  auto cf=cow.GetChangeForm();auto fields=cf.dynamicFields.GetAsJson();
  fields.erase("private.srpOriginalReferenceV1");cf.dynamicFields=DynamicFields::FromJson(fields);
  cow.ApplyChangeForm(cf);
  REQUIRE(SrpOriginalLifecycle::Managed(cow));
  REQUIRE_THROWS(SrpOriginalLifecycle::Inspect(cow));
  REQUIRE_THROWS(cow.Kill(nullptr,false));
  REQUIRE_THROWS(w.worldState.DestroyForm<MpActor>(cowId));
  const auto before=cow.GetInventory().ToJson();
  cow.Respawn();cow.RespawnWithDelay();cow.SetIsDead(false);w.Tick();
  REQUIRE(cow.IsDead());REQUIRE_FALSE(cow.IsRespawning());
  REQUIRE(cow.GetInventory().ToJson()==before);
  REQUIRE(cow.GetDynamicFields().GetAsJson()==fields);
  // Even a contradictory alive bit must not mint a replacement identity.
  cf.isDead=false;cow.ApplyChangeForm(cf);
  REQUIRE_THROWS_WITH(SrpOriginalReference::Adopt(cow,{{"speciesId","offline-cow"},{"policyRevision","R382-observation"}}),"SRP_ORIGINAL_LIFECYCLE_ORPHANED");
  REQUIRE(cow.GetDynamicFields().GetAsJson()==fields);
}
