#include "SrpNpcAdmission.h"
#include "ActionListener.h"
#include "HitMessage.h"
#include "SrpOriginalLifecycle.h"
#include "TestUtils.hpp"
PartOne& GetPartOne();
namespace {
nlohmann::json Config()
{
  return {
    { "schemaVersion", 1 },
    { "enabled", true },
    { "limits",
      { { "total", 3 }, { "fauna", 2 }, { "other", 2 }, { "perRegion", 2 } } },
    { "sources",
      nlohmann::json::array(
        { { { "plugin", "Skyrim.esm" },
            { "category", "fauna" },
            { "references",
              nlohmann::json::array({ "10ebaf:Skyrim.esm" }) } } }) },
    { "faunaBases", nlohmann::json::array() }
  };
}
}
TEST_CASE(
  "R38.3 admission reserves before activation and shares exact identity caps",
  "[srp-r383]")
{
  SrpNpcAdmission gate;
  gate.Configure(Config());
  gate.Reserve(1, "10ebaf:Skyrim.esm", "1:Skyrim.esm", "A", false);
  gate.Reserve(0xff000001, "1", "2:Skyrim.esm", "A", false);
  REQUIRE_THROWS_WITH(
    gate.Reserve(0xff000002, "2", "2:Skyrim.esm", "A", false),
    "SRP_NPC_ADMISSION_REGION_LIMIT");
  REQUIRE(gate.Inspect().at("total") == 2);
  gate.Reserve(0xff000002, "2", "2:Skyrim.esm", "B", false);
  REQUIRE_THROWS_WITH(
    gate.Reserve(0xff000003, "3", "2:Skyrim.esm", "C", false),
    "SRP_NPC_ADMISSION_TOTAL_LIMIT");
  REQUIRE_THROWS(
    gate.Reserve(1, "10ebaf:Skyrim.esm", "1:Skyrim.esm", "A", false));
  gate.Reserve(0xff000004, "4", "7:Skyrim.esm", "A", true);
  REQUIRE(gate.Inspect().at("total") == 3);
  gate.Release(1);
  REQUIRE(gate.Inspect().at("total") == 2);
  REQUIRE_THROWS_WITH(
    gate.Reserve(2, "abc:Skyrim.esm", "1:Skyrim.esm", "A", false),
    "SRP_NPC_ADMISSION_REFERENCE_NOT_APPROVED");
}
TEST_CASE(
  "R38.3 movement refuses a full region without altering the old lease",
  "[srp-r383]")
{
  SrpNpcAdmission gate;
  auto c = Config();
  c["limits"]["perRegion"] = 1;
  gate.Configure(c);
  gate.Reserve(0xff000001, "1", "2:Skyrim.esm", "A", false);
  gate.Reserve(0xff000002, "2", "2:Skyrim.esm", "B", false);
  auto before = gate.Inspect();
  REQUIRE_THROWS(gate.Move(0xff000001, "B"));
  REQUIRE(gate.Inspect() == before);
  REQUIRE(gate.Move(0xff000001, "C"));
  REQUIRE_FALSE(gate.Move(0xff000001, "C"));
  gate.Release(0xff000001);
  REQUIRE(gate.Inspect().at("regions").contains("B"));
  REQUIRE_FALSE(gate.Inspect().at("regions").contains("C"));
}
TEST_CASE("R38.3 winning override source cannot bypass the whitelist through "
          "a master FormDesc",
          "[srp-r383]")
{
  auto c = Config();
  c["sources"][0]["plugin"] = "Override.esp";
  SrpNpcAdmission g;
  g.Configure(c);
  g.Reserve(0x10ebaf, "10ebaf:Skyrim.esm", "1:Skyrim.esm", "A", false,
            "Override.esp");
  REQUIRE_THROWS_WITH(g.Reserve(0x10ebae, "10ebae:Skyrim.esm", "1:Skyrim.esm",
                                "B", false, "Override.esp"),
                      "SRP_NPC_ADMISSION_REFERENCE_NOT_APPROVED");
  REQUIRE_THROWS_WITH(
    g.Reserve(0xff000001, "1", "1:Override.esp", "B", false, "Override.esp"),
    "SRP_NPC_ADMISSION_REFERENCE_NOT_APPROVED");
  g.ResetLeases();
  REQUIRE(g.Enabled());
  REQUIRE(g.Inspect().at("total") == 0);
}
TEST_CASE("R38.3 invalid configuration cannot raise administration maxima or "
          "replace active leases",
          "[srp-r383]")
{
  for (auto [key, value] :
       std::vector<std::pair<std::string, int>>{ { "total", 301 },
                                                 { "fauna", 181 },
                                                 { "other", 121 },
                                                 { "perRegion", 0 } }) {
    SrpNpcAdmission g;
    auto c = Config();
    c["limits"][key] = value;
    REQUIRE_THROWS(g.Configure(c));
    REQUIRE_FALSE(g.Enabled());
  }
  SrpNpcAdmission g;
  g.Configure(Config());
  g.Reserve(0xff000001, "1", "2:Skyrim.esm", "A", false);
  REQUIRE_THROWS(g.Configure(Config()));
  REQUIRE(g.Inspect().at("total") == 1);
}
TEST_CASE("R38.3 actual world rejects original before load and isolates "
          "dynamic actors and players",
          "[srp-r383][espm]")
{
  auto& p = GetPartOne();
  auto& w = p.worldState;
  auto cfg = Config();
  cfg["sources"][0]["references"] = nlohmann::json::array();
  w.ConfigureSrpNpcAdmission(cfg);
  w.npcEnabled = true;
  REQUIRE_FALSE(w.LookupFormById(0x10ebaf));
  REQUIRE(w.GetLoadedActors(0).empty());
  REQUIRE(w.InspectSrpNpcAdmission().at("total") == 0);
  cfg["sources"] = nlohmann::json::array();
  w.ConfigureSrpNpcAdmission(cfg);
  w.npcEnabled = false;
  const auto original = w.GetEspm().GetBrowser().LookupById(0x10ebaf);
  const auto base =
    original.ToGlobalId(reinterpret_cast<const espm::ACHR*>(original.rec)
                          ->GetData(w.GetEspmCache())
                          .baseId);
  auto create = [&](uint32_t id, float x) {
    w.AddForm(
      std::make_unique<MpActor>(
        LocationalData{ { x, 0, 0 }, { 0, 0, 0 }, FormDesc::Tamriel() },
        FormCallbacks::DoNothing(), base),
      id);
  };
  create(0xff000010, 0);
  create(0xff000011, 8192);
  auto& first = w.GetFormAt<MpActor>(0xff000010);
  auto& second = w.GetFormAt<MpActor>(0xff000011);
  const auto before = first.GetPos();
  cfg["limits"]["perRegion"] = 1;
  REQUIRE_THROWS(w.ConfigureSrpNpcAdmission(cfg));
  REQUIRE_THROWS(create(0xff000012, 16384)); // category cap
  p.CreateActor(0xff000001, { 0, 0, 0 }, 0, 0x3c, 10);
  REQUIRE(w.GetLoadedActors(255).size() == 3);
  REQUIRE(w.InspectSrpNpcAdmission().at("total") == 2);
  w.DestroyForm<MpActor>(second.GetFormId());
  create(0xff000012, 16384);
  REQUIRE(w.InspectSrpNpcAdmission().at("total") == 2);
  REQUIRE(first.GetPos() == before);
}
TEST_CASE("R38.3 malformed canonical descriptors are rejected before legacy "
          "resolution",
          "[srp-r383]")
{
  for (const auto& desc :
       { "10ebafjunk:Skyrim.esm", "1000000:Skyrim.esm", ":Skyrim.esm",
         "1:../Skyrim.esm", "1:Skyrim.esm:other.esp" }) {
    auto cfg = Config();
    cfg["sources"][0]["references"] = { desc };
    SrpNpcAdmission g;
    REQUIRE_THROWS(g.Configure(cfg));
    REQUIRE_FALSE(g.Enabled());
  }
}
TEST_CASE("R38.3 actual actor movement and failed saved-image load never "
          "undercount loaded identities",
          "[srp-r383][espm]")
{
  auto& p = GetPartOne();
  auto& w = p.worldState;
  auto cfg = Config();
  cfg["sources"] = nlohmann::json::array();
  cfg["limits"]["perRegion"] = 1;
  w.ConfigureSrpNpcAdmission(cfg);
  const auto original = w.GetEspm().GetBrowser().LookupById(0x10ebaf);
  const auto base =
    original.ToGlobalId(reinterpret_cast<const espm::ACHR*>(original.rec)
                          ->GetData(w.GetEspmCache())
                          .baseId);
  auto create = [&](uint32_t id, float x,
                    const MpChangeForm* saved = nullptr) {
    w.AddForm(
      std::make_unique<MpActor>(
        LocationalData{ { x, 0, 0 }, { 0, 0, 0 }, FormDesc::Tamriel() },
        FormCallbacks::DoNothing(), base),
      id, false, saved);
  };
  create(0xff000010, 0);
  create(0xff000011, 8192);
  auto& first = w.GetFormAt<MpActor>(0xff000010);
  const auto pos = first.GetPos();
  const auto budget = w.InspectSrpNpcAdmission();
  REQUIRE_THROWS(first.SetPos({ 8192, 0, 0 }));
  REQUIRE(first.GetPos() == pos);
  REQUIRE(w.InspectSrpNpcAdmission() == budget);
  first.SetPos({ 16384, 0, 0 });
  REQUIRE(first.GetPos().x == 16384);
  w.DestroyForm<MpActor>(0xff000011);
  auto bad = first.GetChangeForm();
  bad.recType = MpChangeForm::REFR;
  bad.position = { 8192, 0, 0 };
  bad.formDesc = FormDesc::FromFormId(0xff000012, w.espmFiles);
  REQUIRE_THROWS(create(0xff000012, 8192, &bad));
  REQUIRE(w.GetLoadedActors(255).size() == 2);
  REQUIRE(w.InspectSrpNpcAdmission().at("total") == 2);
  REQUIRE_THROWS_WITH(w.LookupFormById(0xff000012),
                      "SRP_NPC_INITIALIZATION_FAILED_RESTART_REQUIRED");
  REQUIRE_THROWS_WITH(w.Tick(),
                      "SRP_NPC_INITIALIZATION_FAILED_RESTART_REQUIRED");
  REQUIRE_THROWS_WITH(w.LookupFormByIdNoLoad(0xff000012),
                      "SRP_NPC_INITIALIZATION_FAILED_RESTART_REQUIRED");
  REQUIRE(w.InspectSrpNpcAdmission().at("initializationFailed") == true);
  REQUIRE(w.GetChangeFormGeneration(0xff000012) == 0);
  REQUIRE_FALSE(w.IsChangeFormSaved(0xff000012, 1));
  REQUIRE_THROWS(create(0xff000013, 24576));
  w.Clear();
  REQUIRE(w.InspectSrpNpcAdmission().at("total") == 0);
  REQUIRE_THROWS_WITH(w.Tick(),
                      "SRP_NPC_INITIALIZATION_FAILED_RESTART_REQUIRED");
}
TEST_CASE("R38.3 controlled original is quarantined before identity adoption",
          "[srp-r383][espm]")
{
  auto& p = GetPartOne();
  auto& w = p.worldState;
  w.ConfigureSrpNpcAdmission(Config());
  w.npcEnabled = true;
  auto& cow = w.GetFormAt<MpActor>(0x10ebaf);
  w.npcEnabled = false;
  REQUIRE(SrpOriginalLifecycle::Managed(cow));
  REQUIRE_THROWS(cow.Kill(nullptr, false));
  REQUIRE(w.InspectSrpNpcAdmission().at("fauna") == 1);
  REQUIRE(w.GetLoadedActors(0) == std::vector<uint32_t>{ 0x10ebaf });
}
TEST_CASE(
  "R38.3 administrative 180 plus 120 budgets share one 300 identity limit",
  "[srp-r383]")
{
  auto c = Config();
  c["limits"] = {
    { "total", 300 }, { "fauna", 180 }, { "other", 120 }, { "perRegion", 300 }
  };
  c["sources"] = nlohmann::json::array();
  c["faunaBases"] = { "1:Skyrim.esm" };
  SrpNpcAdmission g;
  g.Configure(c);
  for (uint32_t i = 0; i < 180; ++i)
    g.Reserve(0xff000000 + i, std::to_string(i), "1:Skyrim.esm", "A", false);
  REQUIRE_THROWS_WITH(
    g.Reserve(0xff001000, "4096", "1:Skyrim.esm", "B", false),
    "SRP_NPC_ADMISSION_CATEGORY_LIMIT");
  for (uint32_t i = 0; i < 120; ++i)
    g.Reserve(0xff002000 + i, std::to_string(i), "2:Skyrim.esm", "A", false);
  REQUIRE(g.Inspect().at("total") == 300);
  REQUIRE(g.Inspect().at("fauna") == 180);
  REQUIRE(g.Inspect().at("other") == 120);
  REQUIRE_THROWS(g.Reserve(0xff003000, "4096", "2:Skyrim.esm", "B", false));
  g.Release(0xff000000);
  g.Reserve(0xff001000, "4096", "1:Skyrim.esm", "B", false);
  REQUIRE(g.Inspect().at("total") == 300);
}
TEST_CASE("R38.3 two player hit packets cannot damage an unapproved original "
          "or mint rewards",
          "[srp-r383][espm]")
{
  auto& p = GetPartOne();
  auto& w = p.worldState;
  w.ConfigureSrpNpcAdmission(Config());
  w.npcEnabled = true;
  auto& cow = w.GetFormAt<MpActor>(0x10ebaf);
  w.npcEnabled = false;
  const auto before = MpChangeForm::ToJson(cow.GetChangeForm());
  for (uint32_t i = 0; i < 2; ++i) {
    DoConnect(p, i);
    p.CreateActor(0xff000001 + i, cow.GetPos(), 0,
                  cow.GetCellOrWorld().ToFormId(w.espmFiles), 101 + i);
    p.SetUserActor(i, 0xff000001 + i);
    auto& player = w.GetFormAt<MpActor>(0xff000001 + i);
    Inventory::ExtraData worn;
    worn.worn_ = true;
    player.AddItem(80254, 1);
    Equipment e;
    e.inv.entries.push_back(Inventory::Entry(80254, 1, worn));
    player.SetEquipment(e);
    HitMessage msg;
    msg.data.aggressor = 0x14;
    msg.data.target = cow.GetFormId();
    msg.data.source = 80254;
    RawMessageData raw;
    raw.userId = i;
    p.GetActionListener().OnHit(raw, msg);
  }
  REQUIRE(MpChangeForm::ToJson(cow.GetChangeForm()) == before);
  REQUIRE_FALSE(cow.IsDead());
}

TEST_CASE("Original hit packets cannot materialize cold targets or unhosted aggressors",
          "[srp-original-combat][espm]")
{
  auto& p=GetPartOne();auto& w=p.worldState;
  w.ConfigureSrpNpcAdmission(Config());
  DoConnect(p,0);
  p.CreateActor(0xff000001,{0,0,0},0,0x3c,9101);
  p.SetUserActor(0,0xff000001);
  auto& player=w.GetFormAt<MpActor>(0xff000001);
  const auto before=MpChangeForm::ToJson(player.GetChangeForm());
  REQUIRE_FALSE(w.LookupFormByIdNoLoad(0x10ebaf));
  w.npcEnabled=true;
  HitMessage msg;msg.data.aggressor=0x14;msg.data.target=0x10ebaf;msg.data.source=80254;
  RawMessageData raw;raw.userId=0;
  REQUIRE_NOTHROW(p.GetActionListener().OnHit(raw,msg));
  REQUIRE_FALSE(w.LookupFormByIdNoLoad(0x10ebaf));
  msg.data.aggressor=0x10ebaf;msg.data.target=0x14;
  REQUIRE_NOTHROW(p.GetActionListener().OnHit(raw,msg));
  REQUIRE_FALSE(w.LookupFormByIdNoLoad(0x10ebaf));
  REQUIRE(MpChangeForm::ToJson(player.GetChangeForm())==before);
  REQUIRE(w.GetChangeFormGeneration(0x10ebaf)==0);
}
