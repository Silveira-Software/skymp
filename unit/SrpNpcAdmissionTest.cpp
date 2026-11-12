#include "SrpNpcAdmission.h"
#include "ActionListener.h"
#include "HitMessage.h"
#include "SpellCastMessage.h"
#include "ActivateMessage.h"
#include "TakeItemMessage.h"
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

TEST_CASE("Reviewed spawn bases use fauna slots without admitting originals",
          "[srp-r383][srp-reviewed-spawn]")
{
  auto config = Config();
  config["sources"][0]["category"] = "other";
  config["faunaBases"] = { "23abe:Skyrim.esm", "23abe:Foreign.esp" };
  config["limits"]["fauna"] = 1;
  SrpNpcAdmission gate;
  gate.Configure(config);

  gate.Reserve(0xff000001, "1", "23abe:Skyrim.esm", "A", false, "Skyrim.esm");
  REQUIRE(gate.Inspect().at("fauna") == 1);
  REQUIRE_THROWS_WITH(gate.Reserve(0xff000002, "2", "23abe:Skyrim.esm", "B", false,
                                   "Skyrim.esm"), "SRP_NPC_ADMISSION_CATEGORY_LIMIT");
  REQUIRE_THROWS_WITH(gate.Reserve(0xff000002, "2", "23abf:Skyrim.esm", "B", false,
                                   "Skyrim.esm"), "SRP_NPC_ADMISSION_REFERENCE_NOT_APPROVED");
  REQUIRE_THROWS_WITH(gate.Reserve(0x10ebae, "10ebae:Skyrim.esm", "23abe:Skyrim.esm", "B", false,
                                   "Skyrim.esm"), "SRP_NPC_ADMISSION_REFERENCE_NOT_APPROVED");
  gate.Reserve(0x10ebaf, "10ebaf:Skyrim.esm", "23abe:Skyrim.esm", "B", false, "Skyrim.esm");
  REQUIRE(gate.Inspect().at("other") == 1);
  gate.Release(0xff000001);
  gate.Reserve(0xff000001, "1", "23abe:Skyrim.esm", "A", false, "Skyrim.esm");

  SrpNpcAdmission restarted;
  restarted.Configure(config);
  restarted.Reserve(0xff000001, "1", "23abe:Skyrim.esm", "A", false, "Skyrim.esm");
  REQUIRE(restarted.Inspect().at("fauna") == 1);
  REQUIRE_THROWS_WITH(restarted.Reserve(0xff000002, "2", "23abe:Foreign.esp", "B", false,
                                        "Skyrim.esm"), "SRP_NPC_ADMISSION_REFERENCE_NOT_APPROVED");
}

TEST_CASE("World rejects spawn allowlist entries that are ACHR records",
          "[srp-r383][srp-reviewed-spawn][espm]")
{
  auto& world = GetPartOne().worldState;
  auto config = Config();
  config["faunaBases"] = { "10ebaf:Skyrim.esm" };
  REQUIRE_THROWS_WITH(world.ConfigureSrpNpcAdmission(config),
                      "SRP_NPC_ADMISSION_BASE_REQUIRED");
  REQUIRE(world.InspectSrpNpcAdmission().at("enabled") == false);
}

TEST_CASE("Opted-in fauna reloads its persisted respawn deadline and clears it on return",
          "[srp-reviewed-spawn][srp-spawn-respawn][espm]")
{
  auto config=Config();
  config["sources"]=nlohmann::json::array();
  constexpr uint32_t id=0xff000077, base=0x23abe;
  auto create=[&](PartOne& server,const MpChangeForm* saved=nullptr) -> MpActor& {
    server.worldState.ConfigureSrpNpcAdmission(config);
    server.worldState.AddForm(std::make_unique<MpActor>(
      LocationalData{{0,0,0},{0,0,0},FormDesc::Tamriel()},FormCallbacks::DoNothing(),base),id,false,saved);
    return server.worldState.GetFormAt<MpActor>(id);
  };
  auto& first=GetPartOne();
  auto& actor=create(first);
  actor.SetPropertyValueDump("srpSpawnSource","\"FAUNA\"",false,false);
  actor.SetPropertyValueDump("srpMobInstanceId","\"reviewed-1\"",false,false);
  actor.SetPropertyValueDump("private.srpSpawnRespawnV1",
    R"({"schemaVersion":1,"instanceId":"reviewed-1","dueAtMs":null})",false,false);
  actor.SetRespawnTime(.15f);
  actor.Kill(nullptr,false);
  const auto state=actor.GetDynamicFields().GetAsJson().at("private.srpSpawnRespawnV1");
  REQUIRE(state.at("dueAtMs").is_number_integer());
  actor.RespawnWithDelay();
  REQUIRE(actor.GetDynamicFields().GetAsJson().at("private.srpSpawnRespawnV1")==state);
  const auto saved=actor.GetChangeForm();
  auto& restarted=GetPartOne();
  auto& loaded=create(restarted,&saved);
  REQUIRE(loaded.IsDead());
  REQUIRE(loaded.IsRespawning());
  REQUIRE(loaded.GetDynamicFields().GetAsJson().at("private.srpSpawnRespawnV1")==state);
  for(int i=0;i<400&&loaded.IsDead();i++) {
    restarted.Tick();std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  REQUIRE_FALSE(loaded.IsDead());
  REQUIRE(loaded.GetDynamicFields().GetAsJson().at("private.srpSpawnRespawnV1").at("dueAtMs").is_null());
  loaded.Kill(nullptr,false);
  REQUIRE(loaded.GetDynamicFields().GetAsJson().at("private.srpSpawnRespawnV1").at("dueAtMs")>=state.at("dueAtMs"));
}

TEST_CASE("A reviewed fauna respawn timer cannot reset disabled or deleted actors",
          "[srp-reviewed-spawn][srp-spawn-respawn][espm]")
{
  auto& server = GetPartOne();
  auto config = Config();
  config["sources"] = nlohmann::json::array();
  server.worldState.ConfigureSrpNpcAdmission(config);
  const uint32_t ref = 0xff000080;
  server.worldState.AddForm(std::make_unique<MpActor>(
    LocationalData{{0,0,0},{0,0,0},FormDesc::Tamriel()},
    FormCallbacks::DoNothing(),0x23abe), ref);
  auto retained = server.worldState.LookupFormByIdNoLoad(ref);
  auto& actor = server.worldState.GetFormAt<MpActor>(ref);
  actor.SetPropertyValueDump("srpSpawnSource", "\"FAUNA\"", false, false);
  actor.SetPropertyValueDump("srpMobInstanceId", "\"retiring\"", false, false);
  actor.SetPropertyValueDump("private.srpSpawnRespawnV1",
    R"({"schemaVersion":1,"instanceId":"retiring","dueAtMs":null})", false, false);
  actor.SetRespawnTime(.05f);
  actor.Kill(nullptr, false);
  const auto marker = actor.GetDynamicFields().GetAsJson().at("private.srpSpawnRespawnV1");
  SECTION("disabled before the scheduled reset") { actor.Disable(); }
  SECTION("deleted before the scheduled reset") { actor.Delete(); }
  for (int tick = 0; tick < 50; ++tick) {
    server.Tick();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  REQUIRE(actor.IsDead());
  REQUIRE(actor.GetDynamicFields().GetAsJson().at("private.srpSpawnRespawnV1") == marker);
}

TEST_CASE("Fauna without a respawn marker keeps the admitted corpse after reload",
          "[srp-reviewed-spawn][srp-spawn-respawn][espm]")
{
  auto& server=GetPartOne();auto config=Config();config["sources"]=nlohmann::json::array();
  server.worldState.ConfigureSrpNpcAdmission(config);
  server.worldState.AddForm(std::make_unique<MpActor>(
    LocationalData{{0,0,0},{0,0,0},FormDesc::Tamriel()},FormCallbacks::DoNothing(),0x23abe),0xff000078);
  auto& actor=server.worldState.GetFormAt<MpActor>(0xff000078);
  actor.SetIsDead(true);
  const auto saved=actor.GetChangeForm();
  auto& restarted=GetPartOne();restarted.worldState.ConfigureSrpNpcAdmission(config);
  restarted.worldState.AddForm(std::make_unique<MpActor>(
    LocationalData{{0,0,0},{0,0,0},FormDesc::Tamriel()},FormCallbacks::DoNothing(),0x23abe),0xff000078,false,&saved);
  const auto& loaded=restarted.worldState.GetFormAt<MpActor>(0xff000078);
  REQUIRE(loaded.IsDead());REQUIRE_FALSE(loaded.IsRespawning());
}

TEST_CASE("A respawn deadline cannot be reassigned to another creature instance",
          "[srp-reviewed-spawn][srp-spawn-respawn][espm]")
{
  auto& server=GetPartOne();auto config=Config();config["sources"]=nlohmann::json::array();
  server.worldState.ConfigureSrpNpcAdmission(config);
  server.worldState.AddForm(std::make_unique<MpActor>(
    LocationalData{{0,0,0},{0,0,0},FormDesc::Tamriel()},FormCallbacks::DoNothing(),0x23abe),0xff000079);
  auto& actor=server.worldState.GetFormAt<MpActor>(0xff000079);
  actor.SetPropertyValueDump("srpSpawnSource","\"FAUNA\"",false,false);
  actor.SetPropertyValueDump("srpMobInstanceId","\"current\"",false,false);
  actor.SetPropertyValueDump("private.srpSpawnRespawnV1",
    R"({"schemaVersion":1,"instanceId":"foreign","dueAtMs":1})",false,false);
  REQUIRE_THROWS_WITH(actor.RespawnWithDelay(),"SRP_SPAWN_RESPAWN_STATE_INVALID");
  REQUIRE_FALSE(actor.IsRespawning());
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

TEST_CASE("Original activation and old loot packets cannot materialize a cold actor",
          "[srp-original-impact][espm]")
{
  auto& p = GetPartOne();
  auto& world = p.worldState;
  world.ConfigureSrpNpcAdmission(Config());
  world.npcEnabled = true;
  REQUIRE_FALSE(world.IsSrpOriginalControlled(0x4cc2d));
  REQUIRE_FALSE(world.LookupFormByIdNoLoad(0x4cc2d));
  p.CreateActor(0xff000090, {0, 0, 0}, 0, 0x165a8, 9100);
  DoConnect(p, 9);
  p.SetUserActor(9, 0xff000090);
  REQUIRE_FALSE(world.LookupFormByIdNoLoad(0x10ebaf));
  const auto budget = world.InspectSrpNpcAdmission();
  RawMessageData message;
  message.userId = 9;
  ActivateMessage activate;
  activate.data.caster = 0x14;
  activate.data.target = 0x10ebaf;
  REQUIRE_NOTHROW(p.GetActionListener().OnActivate(message, activate));
  TakeItemMessage take;
  take.target = 0x10ebaf;
  take.baseId = 15;
  take.count = 1;
  REQUIRE_NOTHROW(p.GetActionListener().OnTakeItem(message, take));
  REQUIRE_FALSE(world.LookupFormByIdNoLoad(0x10ebaf));
  REQUIRE(world.InspectSrpNpcAdmission() == budget);
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

TEST_CASE("Original spell cast packets cannot load cold actors through hosting or target lookup",
          "[srp-original-combat][espm]")
{
  auto& server = GetPartOne();
  auto& world = server.worldState;
  world.ConfigureSrpNpcAdmission(Config());
  DoConnect(server, 0);
  server.CreateActor(0xff000001, {0, 0, 0}, 0, 0x3c, 9101);
  server.SetUserActor(0, 0xff000001);
  auto& player = world.GetFormAt<MpActor>(0xff000001);
  Equipment equipment;
  equipment.leftSpell = 0x12fd0;
  player.SetEquipment(equipment);
  const auto before = MpChangeForm::ToJson(player.GetChangeForm());
  world.npcEnabled = true;
  REQUIRE_FALSE(world.LookupFormByIdNoLoad(0x10ebaf));
  SpellCastMessage message;
  message.data.caster = 0x10ebaf;
  message.data.target = 0x14;
  message.data.spell = 0x12fd0;
  RawMessageData raw;
  raw.userId = 0;
  REQUIRE_NOTHROW(server.GetActionListener().OnSpellCast(raw, message));
  REQUIRE_FALSE(world.LookupFormByIdNoLoad(0x10ebaf));
  // A stale hosting entry still cannot create its missing caster.
  world.hosters[0x10ebaf] = player.GetFormId();
  REQUIRE_NOTHROW(server.GetActionListener().OnSpellCast(raw, message));
  REQUIRE_FALSE(world.LookupFormByIdNoLoad(0x10ebaf));
  message.data.caster = 0x14;
  message.data.target = 0x10ebaf;
  REQUIRE_NOTHROW(server.GetActionListener().OnSpellCast(raw, message));
  REQUIRE_FALSE(world.LookupFormByIdNoLoad(0x10ebaf));
  REQUIRE(world.GetChangeFormGeneration(0x10ebaf) == 0);
  REQUIRE(MpChangeForm::ToJson(player.GetChangeForm()) == before);
}
