#include "SrpOriginalReference.h"
#include "TestUtils.hpp"
#include "libespm/GroupUtils.h"
#include <catch2/catch_all.hpp>
#include "database_drivers/FileDatabase.h"
#include "save_storages/AsyncSaveStorage.h"
#include <filesystem>
#include <fstream>
#include <unistd.h>

PartOne& GetPartOne();
namespace {
const nlohmann::json policy = {{"speciesId", "offline-original"}, {"policyRevision", "review-1"}};
MpActor& Original(PartOne& w)
{
  // Same original Whiterun cow verified in the existing NpcExists.cpp tests.
  // Do not scan/load arbitrary quest actors into the shared unit fixture.
  w.worldState.npcEnabled = true;
  auto& actor = w.worldState.GetFormAt<MpActor>(0x10ebaf);
  w.worldState.npcEnabled = false;
  SrpOriginalReference::Adopt(actor, policy);
  return actor;
}
}
TEST_CASE("R38 original reference identity replays without touching items or resources", "[srp-r38-original]")
{
  auto& w = GetPartOne();
  auto& a = Original(w);
  const auto before = MpChangeForm::ToJson(a.GetChangeForm());
  const auto first = SrpOriginalReference::Adopt(a, policy);
  const auto next = SrpOriginalReference::Adopt(a, policy);
  REQUIRE(first.at("receipt") == next.at("receipt"));
  REQUIRE(next.at("duplicate") == true);
  REQUIRE(next.at("receipt").at("rewardsEnabled") == false);
  REQUIRE(next.at("generation").get<uint64_t>() > 0);
  REQUIRE(MpChangeForm::ToJson(a.GetChangeForm()) == before);
}
TEST_CASE("R38 original adoption rejects policy equivocation and corrupt receipts", "[srp-r38-original]")
{
  auto& w = GetPartOne();
  auto& a = Original(w);
  const auto before = MpChangeForm::ToJson(a.GetChangeForm());
  auto foreign = policy; foreign["speciesId"] = "foreign";
  REQUIRE_THROWS(SrpOriginalReference::Adopt(a, foreign));
  REQUIRE(MpChangeForm::ToJson(a.GetChangeForm()) == before);
  a.SetPropertyValueDump("private.srpOriginalReferenceV1", "{\"schemaVersion\":1}", false, false);
  REQUIRE_THROWS(SrpOriginalReference::Adopt(a, policy));
  REQUIRE(a.GetDynamicFields().GetAsJson().at("private.srpOriginalReferenceV1").size() == 1);
}
TEST_CASE("R38 original adoption cannot turn a player or dynamic reference into fauna", "[srp-r38-original]")
{
  auto& w = GetPartOne();
  w.CreateActor(0xff000000, {0,0,0}, 0, 0x3c, 1);
  REQUIRE_THROWS(SrpOriginalReference::Adopt(w.worldState.GetFormAt<MpActor>(0xff000000), policy));
  auto& a = Original(w);
  a.SetPropertyValueDump("srpActiveCharacterId", "\"offline-player\"", false, false);
  REQUIRE_THROWS(SrpOriginalReference::Adopt(a, policy));
}
TEST_CASE("R38 original identity ACK contains the receipt in the fsynced world record", "[srp-r38-original]")
{
  auto& w=GetPartOne();auto& a=Original(w);
  const auto result=SrpOriginalReference::Adopt(a,policy);
  const auto snapshot=MpChangeForm::ToJson(a.GetChangeForm());
  REQUIRE(snapshot.at("dynamicFields").contains("private.srpOriginalReferenceV1"));
  auto directory=std::filesystem::temp_directory_path()/("srp-original-save-"+std::to_string(getpid()));
  std::filesystem::create_directories(directory);
  using Storage=Viet::AsyncSaveStorage<MpChangeForm,FormDesc,std::vector<FormDesc>>;
  struct TracedDatabase:Viet::IDatabase<MpChangeForm,FormDesc,std::vector<FormDesc>> {
    FileDatabase disk;
    std::vector<std::optional<MpChangeForm>> recycled;
    TracedDatabase(const std::string& path,std::shared_ptr<spdlog::logger> logger,bool sync):disk(path,logger,sync){}
    nlohmann::json seen;
    std::vector<std::optional<MpChangeForm>>&& UpsertImpl(std::vector<std::optional<MpChangeForm>>&& forms,size_t& count)override{
      for(const auto& form:forms)if(form&&form->formDesc.shortFormId==0x10ebaf)seen=MpChangeForm::ToJson(*form);
      count=disk.Upsert(std::move(forms));disk.GetRecycledChangeFormsBuffer(recycled);return std::move(recycled);
    }
    void Iterate(const IterateCallback& callback,std::optional<std::vector<FormDesc>> filter)override{disk.Iterate(callback,filter);}
  };
  auto db=std::make_shared<TracedDatabase>(directory.string(),std::make_shared<spdlog::logger>("original-test"),true);
  w.worldState.AttachSaveStorage(std::make_shared<Storage>(db));
  const auto generation=w.worldState.GetChangeFormGeneration(a.GetFormId());
  for(int n=0;n<2200&&!w.worldState.IsChangeFormSaved(a.GetFormId(),generation);++n){w.Tick();std::this_thread::sleep_for(std::chrono::milliseconds(5));}
  REQUIRE(w.worldState.IsChangeFormSaved(a.GetFormId(),generation));
  INFO("driver snapshot: "<<db->seen.dump());
  REQUIRE(db->seen.at("dynamicFields").contains("private.srpOriginalReferenceV1"));
  auto path=directory/"changeForms"/(FormDesc::FromFormId(a.GetFormId(),w.worldState.espmFiles).ToString('_')+".json");
  REQUIRE(std::filesystem::exists(path));
  std::ifstream file(path);nlohmann::json stored;file>>stored;
  REQUIRE(stored.at("dynamicFields").at("private.srpOriginalReferenceV1")==result.at("receipt"));
}
