#include "SrpNpcAdmission.h"
#include "TestUtils.hpp"
#include "database_drivers/FileDatabase.h"
#include "save_storages/AsyncSaveStorage.h"
#include <filesystem>
#include <unistd.h>
PartOne& GetPartOne();
namespace {
nlohmann::json Policy()
{
  return {
    { "schemaVersion", 1 },
    { "enabled", true },
    { "limits",
      { { "total", 3 }, { "fauna", 2 }, { "other", 2 }, { "perRegion", 1 } } },
    { "sources", nlohmann::json::array() },
    { "faunaBases", nlohmann::json::array() }
  };
}
MpChangeForm Image(WorldState& w, uint32_t id, float x)
{
  MpChangeForm a;
  a.recType = MpChangeForm::ACHR;
  a.formDesc = FormDesc::FromFormId(id, w.espmFiles);
  a.baseDesc = FormDesc::FromString("829b3:Skyrim.esm");
  a.worldOrCellDesc = FormDesc::Tamriel();
  a.position = { x, 0, 0 };
  a.profileId = -1;
  a.inv.entries.push_back(Inventory::Entry(15, 17));
  a.dynamicFields.SetValueDump("coldReceipt",
                               R"({"operation":"retained","committed":true})");
  return a;
}
}
TEST_CASE("R38.4 cold dynamic NPC storage does not consume active budget or "
          "remint IDs",
          "[srp-r384][espm]")
{
  auto& p = GetPartOne();
  auto& w = p.worldState;
  w.ConfigureSrpNpcAdmission(Policy());
  w.npcEnabled = false;
  for (uint32_t i = 0; i < 400; ++i)
    w.LoadChangeForm(Image(w, 0xff001000 + i, float(i) * 8192),
                     FormCallbacks::DoNothing());
  REQUIRE(w.GetLoadedActors(255).empty());
  REQUIRE(w.InspectSrpNpcAdmission().at("total") == 0);
  REQUIRE(w.InspectSrpNpcAdmission().at("deferredDynamicNpcs") == 400);
  REQUIRE(w.GetAllForms(255)->size() == 400);
  auto& a = w.GetFormAt<MpActor>(0xff001000);
  REQUIRE(a.GetInventory().GetItemCount(15) == 17);
  REQUIRE(a.GetDynamicFields().GetAsJson().at("coldReceipt").at("operation") ==
          "retained");
  REQUIRE(a.GetFormId() == 0xff001000);
  REQUIRE(a.GetPos().x == 0);
  REQUIRE(w.InspectSrpNpcAdmission().at("total") == 1);
  REQUIRE(w.InspectSrpNpcAdmission().at("deferredDynamicNpcs") == 399);
  REQUIRE(&w.GetFormAt<MpActor>(0xff001000) == &a);
  w.GetFormAt<MpActor>(0xff001001);
  REQUIRE_FALSE(w.LookupFormById(0xff001002));
  REQUIRE(w.InspectSrpNpcAdmission().at("total") == 2);
  REQUIRE(w.InspectSrpNpcAdmission().at("deferredDynamicNpcs") == 398);
  REQUIRE(w.InspectSrpNpcAdmission().at("initializationFailed") == false);
}
TEST_CASE("R38.4 existing interest grid admits cold NPCs only near their "
          "saved coordinates",
          "[srp-r384][espm]")
{
  auto& p = GetPartOne();
  auto& w = p.worldState;
  w.ConfigureSrpNpcAdmission(Policy());
  w.npcEnabled = false;
  w.LoadChangeForm(Image(w, 0xff002000, 0), FormCallbacks::DoNothing());
  w.LoadChangeForm(Image(w, 0xff002001, 409600), FormCallbacks::DoNothing());
  const auto& near = w.GetNeighborsByPosition(0x3c, 0, 0);
  REQUIRE(w.GetLoadedActors(255) == std::vector<uint32_t>{ 0xff002000 });
  REQUIRE(w.InspectSrpNpcAdmission().at("deferredDynamicNpcs") == 1);
  REQUIRE(near.count(&w.GetFormAt<MpActor>(0xff002000)) == 1);
  w.GetNeighborsByPosition(0x3c, 100, 0);
  REQUIRE(w.GetLoadedActors(255).size() == 2);
}
TEST_CASE(
  "R38.4 player identity and disabled policy preserve eager legacy loading",
  "[srp-r384][espm]")
{
  auto& p = GetPartOne();
  auto& w = p.worldState;
  w.ConfigureSrpNpcAdmission(Policy());
  auto player = Image(w, 0xff003000, 0);
  player.profileId = 314159;
  player.baseDesc = FormDesc::FromString("7:Skyrim.esm");
  w.LoadChangeForm(player, FormCallbacks::DoNothing());
  REQUIRE(w.GetLoadedActors(255).size() == 1);
  REQUIRE(w.InspectSrpNpcAdmission().at("total") == 0);
  REQUIRE(w.InspectSrpNpcAdmission().at("deferredDynamicNpcs") == 0);
  auto& q = GetPartOne();
  auto image = Image(q.worldState, 0xff003001, 0);
  q.worldState.LoadChangeForm(image, FormCallbacks::DoNothing());
  REQUIRE(q.worldState.GetLoadedActors(255).size() == 1);
  REQUIRE(q.worldState.InspectSrpNpcAdmission().at("enabled") == false);
}

TEST_CASE("R38.4 historical off-map NPC images remain cold without deleting "
          "or moving them",
          "[srp-r384][espm]")
{
  auto& p = GetPartOne();
  auto& w = p.worldState;
  w.ConfigureSrpNpcAdmission(Policy());
  w.npcEnabled = false;
  auto parked = Image(w, 0xff004000, -1000000000.f);
  w.LoadChangeForm(parked, FormCallbacks::DoNothing());
  REQUIRE(w.GetLoadedActors(255).empty());
  REQUIRE(w.InspectSrpNpcAdmission().at("deferredDynamicNpcs") == 1);
  w.GetNeighborsByPosition(0x3c, 0, 0);
  REQUIRE(w.GetLoadedActors(255).empty());
  REQUIRE_FALSE(w.LookupFormById(0xff004000));
  REQUIRE(w.InspectSrpNpcAdmission().at("initializationFailed") == false);
  REQUIRE(w.GetAllForms(255)->size() == 1);
}

TEST_CASE(
  "R38.4 new IDs and explicit inserts cannot replace cold durable NPCs",
  "[srp-r384][espm]")
{
  auto& p = GetPartOne();
  auto& w = p.worldState;
  w.ConfigureSrpNpcAdmission(Policy());
  w.npcEnabled = false;
  w.LoadChangeForm(Image(w, 0xff000000, -1000000000.f),
                   FormCallbacks::DoNothing());
  w.LoadChangeForm(Image(w, 0xff000001, 0), FormCallbacks::DoNothing());
  REQUIRE(w.GenerateFormId() == 0xff000002);
  REQUIRE(w.GetLoadedActors(255).empty());
  REQUIRE(w.InspectSrpNpcAdmission().at("deferredDynamicNpcs") == 2);
  REQUIRE_THROWS_WITH(w.AddForm(std::make_unique<MpForm>(), 0xff000000),
                      "SRP_COLD_NPC_ID_RESERVED");
  REQUIRE_THROWS_WITH(w.AddForm(std::make_unique<MpForm>(), 0xff000001, true),
                      "SRP_COLD_NPC_ID_RESERVED");
  auto& actor = w.GetFormAt<MpActor>(0xff000001);
  REQUIRE(actor.GetInventory().GetItemCount(15) == 17);
  REQUIRE(
    actor.GetDynamicFields().GetAsJson().at("coldReceipt").at("committed") ==
    true);
  REQUIRE(w.GetAllForms(255)->size() == 2);
  REQUIRE(w.InspectSrpNpcAdmission().at("total") == 1);
}
TEST_CASE(
  "R38.4 invalid cold recovery cannot look like a successful partial boot",
  "[srp-r384][espm]")
{
  auto& p = GetPartOne();
  auto& w = p.worldState;
  w.ConfigureSrpNpcAdmission(Policy());
  w.npcEnabled = false;
  auto bad = Image(w, 0xff005000, 100000001.f);
  bad.position.z = 200000000.f;
  REQUIRE_THROWS_WITH(w.LoadChangeForm(bad, FormCallbacks::DoNothing()),
                      "SRP_NPC_ADMISSION_POSITION_INVALID");
  REQUIRE(w.InspectSrpNpcAdmission().at("initializationFailed") == true);
  REQUIRE_THROWS_WITH(w.Tick(),
                      "SRP_NPC_INITIALIZATION_FAILED_RESTART_REQUIRED");
  REQUIRE(w.GetLoadedActors(255).empty());
  w.Clear();
  REQUIRE_THROWS_WITH(w.Tick(),
                      "SRP_NPC_INITIALIZATION_FAILED_RESTART_REQUIRED");
}

TEST_CASE(
  "R38.4 durable NPC tombstones reserve IDs without consuming admission",
  "[srp-r384][espm]")
{
  auto& p = GetPartOne();
  auto& w = p.worldState;
  w.ConfigureSrpNpcAdmission(Policy());
  w.npcEnabled = false;
  auto deleted = Image(w, 0xff000000, 0);
  deleted.isDeleted = true;
  w.RetainSrpNpcTombstone(deleted);
  REQUIRE(w.GenerateFormId() == 0xff000001);
  REQUIRE(w.GetLoadedActors(255).empty());
  REQUIRE(w.GetAllForms(255)->size() == 1);
  REQUIRE_FALSE(w.LookupFormById(0xff000000));
  REQUIRE(w.InspectSrpNpcAdmission().at("total") == 0);
}
TEST_CASE("R38.4 deletion without a durable save never frees a native slot",
          "[srp-r384][espm]")
{
  auto& p = GetPartOne();
  auto& w = p.worldState;
  w.ConfigureSrpNpcAdmission(Policy());
  w.npcEnabled = false;
  w.LoadChangeForm(Image(w, 0xff006000, 0), FormCallbacks::DoNothing());
  auto& actor = w.GetFormAt<MpActor>(0xff006000);
  actor.Disable();
  actor.Delete();
  w.Tick();
  REQUIRE(w.InspectSrpNpcAdmission().at("total") == 1);
  REQUIRE(w.InspectSrpNpcAdmission().at("retiringNpcs") == 0);
  REQUIRE(w.LookupFormByIdNoLoad(0xff006000));
}

TEST_CASE("R38.4 retirement blocks recursive removal and runs teardown once",
          "[srp-r384][espm]")
{
  auto& p = GetPartOne();
  auto& w = p.worldState;
  w.ConfigureSrpNpcAdmission(Policy());
  w.npcEnabled = false;
  w.LoadChangeForm(Image(w, 0xff007000, 0), FormCallbacks::DoNothing());
  auto& actor = w.GetFormAt<MpActor>(0xff007000);
  struct Sink : MpActor::DestroyEventSink
  {
    int count = 0;
    void BeforeDestroy(MpActor& a) override
    {
      ++count;
      REQUIRE_THROWS_WITH(a.GetParent()->DestroyForm<MpActor>(a.GetFormId()),
                          "SRP_NPC_RETIREMENT_TEARDOWN_IN_PROGRESS");
    }
  };
  auto sink = std::make_shared<Sink>();
  actor.AddEventSink(sink);
  auto directory = std::filesystem::temp_directory_path() /
    ("srp-retirement-" + std::to_string(getpid()));
  std::filesystem::create_directories(directory);
  using Storage =
    Viet::AsyncSaveStorage<MpChangeForm, FormDesc, std::vector<FormDesc>>;
  auto db = std::make_shared<FileDatabase>(
    directory.string(), std::make_shared<spdlog::logger>("retirement-test"),
    true);
  w.AttachSaveStorage(std::make_shared<Storage>(db));
  actor.Disable();
  actor.Delete();
  for (int n = 0; n < 2200 && w.InspectSrpNpcAdmission().at("total") != 0;
       ++n) {
    w.Tick();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  REQUIRE(w.InspectSrpNpcAdmission().at("total") == 0);
  REQUIRE(w.InspectSrpNpcAdmission().at("retiringNpcs") == 0);
  REQUIRE(w.InspectSrpNpcAdmission().at("reservedNpcTombstones") == 1);
  REQUIRE(sink->count == 1);
  REQUIRE_FALSE(w.LookupFormByIdNoLoad(0xff007000));
}
