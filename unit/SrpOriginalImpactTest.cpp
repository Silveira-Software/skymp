#include "SrpOriginalImpact.h"
#include "ActionListener.h"
#include "CustomPacketMessage.h"
#include "FormCallbacks.h"
#include "HitMessage.h"
#include "SrpCollision.h"
#include "SrpActorStats.h"
#include "SrpOriginalReference.h"
#include "SrpOriginalStats.h"
#include "SrpOriginalLifecycle.h"
#include "MpActor.h"
#include "PartOne.h"
#include "DynamicFields.h"
#include "database_drivers/FileDatabase.h"
#include "formulas/TES5DamageFormula.h"
#include "save_storages/AsyncSaveStorage.h"
#include "gamemode_events/GameModeEvent.h"
#include <catch2/catch_all.hpp>
#include <csignal>
#include <filesystem>
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>

PartOne& GetPartOne();

namespace {
constexpr uint32_t cowId = 0x10ebaf;
constexpr uint32_t playerId = 0xff000091;
constexpr Networking::UserId testUserId = 7;

std::shared_ptr<FileDatabase> AttachStorage(
  PartOne& w, const std::filesystem::path& path =
    std::filesystem::temp_directory_path() /
      ("srp-impact-" + std::to_string(getpid())))
{
  using Save = Viet::AsyncSaveStorage<MpChangeForm, FormDesc, std::vector<FormDesc>>;
  auto database = std::make_shared<FileDatabase>(
    path.string(), std::make_shared<spdlog::logger>("impact"), true);
  w.worldState.AttachSaveStorage(std::make_shared<Save>(database));
  return database;
}

void WaitForSaved(PartOne& w, MpActor& actor)
{
  const auto g = w.worldState.GetChangeFormGeneration(actor.GetFormId());
  for (int i = 0; i < 2200 && !w.worldState.IsChangeFormSaved(actor.GetFormId(), g); ++i) {
    w.Tick();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  REQUIRE(w.worldState.IsChangeFormSaved(actor.GetFormId(), g));
}

// Every impact case shares one PartOne (GetPartOne) and the same cow/player
// forms, so a previous case can leave a durable ledger, a pending operation or
// an installed scene behind. Cases therefore start from an explicit empty
// impact state and choose their own scene, which keeps them order-independent.
void ClearImpactState(MpActor& actor)
{
  auto cf = actor.GetChangeForm();
  auto fields = cf.dynamicFields.GetAsJson();
  fields.erase(SrpOriginalImpact::kProperty);
  fields.erase(SrpOriginalImpact::kIdentityProperty);
  cf.dynamicFields = DynamicFields::FromJson(fields);
  actor.ApplyChangeForm(cf);
}

void Isolate(PartOne& w, MpActor& cow, MpActor& player)
{
  w.worldState.InstallSrpCollisionScene(nullptr);
  ClearImpactState(cow);
  ClearImpactState(player);
  WaitForSaved(w, cow);
  WaitForSaved(w, player);
}

// A dynamic snapshot is only complete once the installed audit enumerates the
// cell's dynamic blockers and none of them is live. These cases exercise that
// gate explicitly instead of assuming an empty world.
nlohmann::json AuditConfig(uint32_t cellId, bool complete,
                           std::initializer_list<uint32_t> blockers = {})
{
  nlohmann::json listed = nlohmann::json::array();
  for (auto id : blockers)
    listed.push_back(id);
  return {{"schemaVersion", 1},
          {"dynamicAudit", {{"schemaVersion", 1},
                            {"worldOrCell", cellId},
                            {"complete", complete},
                            {"blockers", listed}}}};
}

nlohmann::json ActorShape(const std::string& baseDesc, const std::string& raceDesc)
{
  return {{"baseDesc", baseDesc},
          {"raceDesc", raceDesc},
          {"policy", "SERVER_MESH_PROXY_V1"},
          {"representation", "havok-triangles"},
          {"asset", "cow-proxy.nif"},
          {"localTransform", {1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, 0}},
          {"vertices", {{-40, -40, 0}, {40, -40, 0}, {40, 40, 128}, {-40, 40, 128}}},
          {"triangles", {{0, 1, 2}, {0, 2, 3}}}};
}

std::shared_ptr<const SrpCollision::Scene> CreateSyntheticScene(
  uint32_t cellId, const std::string& baseDesc, const std::string& raceDesc,
  const NiPoint3& center, const std::string& attackerBase,
  const std::string& attackerRace, bool controllerEnvelope = false,
  bool wall = false)
{
  const std::string dummyHash(64, 'a');
  // The audited coverage box must actually contain the real fixture position.
  // A fixed world-space box would silently drop the coverage region (and thus
  // never prove the attack segment clear) whenever the fixture sits far from
  // the origin, so the box is anchored on the target instead.
  const double x = center.x, y = center.y, z = center.z;
  nlohmann::json shapes = nlohmann::json::array(
    {ActorShape(baseDesc, raceDesc), ActorShape(attackerBase, attackerRace)});
  if (controllerEnvelope) {
    auto& shape = shapes.at(1);
    shape["sex"] = 0;
    shape["representation"] = "havok-controller-inscribed-v1";
    shape["blockingBounds"] = {{"min", {-41, -41, -1}}, {"max", {41, 41, 129}}};
  }
  nlohmann::json doc = {
    {"schemaVersion", 1},
    {"units", "skyrim-world"},
    {"bindings", {
      {"loadOrder", nlohmann::json::array({{{"name", "fixture.esm"}, {"sha256", dummyHash}}})},
      {"assets", nlohmann::json::array({{{"path", "cow-proxy.nif"}, {"sha256", dummyHash}}})},
      {"producerSha256", dummyHash},
      {"coverageAuditSha256", dummyHash}
    }},
    {"geometry", nlohmann::json::array()},
    {"coverage", nlohmann::json::array({{
      {"worldOrCell", cellId},
      {"min", {x - 10000.0, y - 10000.0, z - 10000.0}},
      {"max", {x + 10000.0, y + 10000.0, z + 10000.0}},
      {"complete", true}
    }})},
    {"unresolved", nlohmann::json::array()},
    {"actorShapes", shapes}
  };
  if (wall) {
    const auto wallY = y - 25.0;
    doc["geometry"].push_back({
      {"worldOrCell", cellId}, {"reference", "1:fixture.esm"},
      {"base", "2:fixture.esm"}, {"asset", "cow-proxy.nif"},
      {"representation", "havok-triangles"},
      {"worldTransform", {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0}},
      {"referenceTransform", {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0}},
      {"vertices", {{x-50, wallY, z}, {x+50, wallY, z},
                    {x+50, wallY, z+128}, {x-50, wallY, z+128}}},
      {"triangles", {{0, 1, 2}, {0, 2, 3}}}
    });
  }
  return SrpCollision::Scene::Load(doc, doc.at("bindings"), SrpCollision::Scene::Digest(doc));
}

MpActor& SetupCow(PartOne& w)
{
  w.worldState.npcEnabled = true;
  auto& cow = w.worldState.GetFormAt<MpActor>(cowId);
  w.worldState.npcEnabled = false;
  SrpOriginalReference::Adopt(cow, {{"speciesId", "cow"}, {"policyRevision", "impact-1"}});
  WaitForSaved(w, cow);
  cow.ApplySrpOriginalStats();
  WaitForSaved(w, cow);
  return cow;
}

MpActor& SetupPlayer(PartOne& w, const MpActor& target)
{
  w.serverState.Connect(testUserId, "test-guid-1");
  // Place attacker 50 units behind target on Y axis, facing Y (angle.z = 0)
  NiPoint3 pos = target.GetPos();
  pos.y -= 50.0f;
  const uint32_t cellId = target.GetCellOrWorld().ToFormId(w.worldState.espmFiles);
  w.CreateActor(playerId, pos, 0, cellId, 9191);
  auto& player = w.worldState.GetFormAt<MpActor>(playerId);
  w.serverState.actorsMap.Set(testUserId, &player);

  player.SetPropertyValueDump("srpActiveCharacterId", "\"combat-PC\"", false, false);
  nlohmann::json proj = {
    {"schemaVersion", 2}, {"rulesVersion", "SRP-PROGRESSION-A1"}, {"revision", 1},
    {"profileId", "9191"}, {"characterId", "combat-PC"}, {"level", 10},
    {"attributes", {{"vigor", 0}, {"strength", 5}, {"dexterity", 0}, {"intelligence", 0}, {"willpower", 0}, {"constitution", 5}}}
  };
  player.SetPropertyValueDump("srpProgression", proj.dump(), false, false);
  player.ApplySrpActorStats(proj);
  WaitForSaved(w, player);
  return player;
}
}

TEST_CASE("Collision pose rejects bone overrides and reads authoritative actor sex", "[srp-original-impact]")
{
  auto& w=GetPartOne();AttachStorage(w);
  auto& cow=SetupCow(w);
  auto& player=SetupPlayer(w,cow);
  const auto before=player.GetChangeForm();
  const auto pose=w.worldState.CaptureSrpActorCollisionPose(player);
  REQUIRE(pose.has_value());
  REQUIRE(pose->at(3)==player.GetPos().x);
  REQUIRE(pose->at(7)==player.GetPos().y);
  REQUIRE(pose->at(11)==player.GetPos().z);
  Appearance appearance;
  appearance.raceId=player.GetRaceId();appearance.isFemale=true;
  player.SetAppearance(&appearance);
  REQUIRE(player.IsFemale());
  appearance.isFemale=false;player.SetAppearance(&appearance);
  REQUIRE_FALSE(player.IsFemale());
  player.SetNodeScale("NPC Root [Root]",2.f,false);
  REQUIRE_FALSE(w.worldState.CaptureSrpActorCollisionPose(player).has_value());
  player.ApplyChangeForm(before);
}

TEST_CASE("Impact fails closed when server collision scene is not installed", "[srp-original-impact]")
{
  auto& w = GetPartOne();
  AttachStorage(w);
  auto& cow = SetupCow(w);
  auto& player = SetupPlayer(w, cow);
  Isolate(w, cow, player);

  REQUIRE_THROWS_WITH(
    SrpOriginalImpact::Begin(w, testUserId, cowId, "req-no-scene-1"),
    "SRP_IMPACT_SERVER_FRAME_UNAVAILABLE"
  );
}

TEST_CASE("Versioned original melee binds a durable action to the connected character",
          "[srp-original-impact][srp-r46-melee]")
{
  using Json = nlohmann::json;
  struct ImpactListener : PartOne::Listener
  {
    PartOne* server = nullptr;
    std::vector<Json> requests;
    Json result;
    void OnConnect(Networking::UserId) override {}
    void OnDisconnect(Networking::UserId) override {}
    void OnCustomPacket(Networking::UserId, const simdjson::dom::element&) override {}
    bool OnMpApiEvent(const GameModeEvent& event) override
    {
      if (!server || std::string(event.GetName()) != "onSrpOriginalImpactRequest")
        return true;
      const auto args = Json::parse(event.GetArgumentsJsonArray());
      requests.push_back(args);
      result = SrpOriginalImpact::Begin(*server, args.at(0).get<Networking::UserId>(),
        args.at(1).get<uint32_t>(), args.at(2).get<std::string>());
      return false;
    }
  };
  auto& server = GetPartOne();
  const auto database = AttachStorage(server);
  auto& cow = SetupCow(server);
  auto& player = SetupPlayer(server, cow);
  Isolate(server, cow, player);
  auto& world = server.worldState;
  const auto desc = [&](uint32_t id) { return FormDesc::FromFormId(id, world.espmFiles).ToString(); };
  const auto cell = cow.GetCellOrWorld().ToFormId(world.espmFiles);
  world.InstallSrpCollisionScene(CreateSyntheticScene(cell, desc(cow.GetBaseId()),
    desc(cow.GetRaceId()), cow.GetPos(), desc(player.GetBaseId()), desc(player.GetRaceId())),
    AuditConfig(cell, true));
  auto listener = std::make_shared<ImpactListener>();
  server.AddListener(listener);
  listener->server = &server;
  const auto send = [&](const Json& payload) {
    RawMessageData raw;
    raw.userId = testUserId;
    CustomPacketMessage packet;
    packet.contentJsonDump = payload.dump();
    server.GetActionListener().OnCustomPacket(raw, packet);
  };
  const Json hello = {{"customPacketType", "srpOriginalMelee"}, {"version", 1},
                      {"action", "hello"}};
  const auto negotiate = [&] {
    server.Messages().clear();
    send(hello);
    REQUIRE(server.Messages().size() == 1);
    const auto ready = Json::parse(server.Messages().back().j.at("contentJsonDump").get<std::string>());
    REQUIRE(ready.at("action") == "ready");
    REQUIRE(ready.at("actorId") == playerId);
    return ready.at("session").get<std::string>();
  };
  const auto session = negotiate();
  Json impact = hello;
  impact["action"] = "impact";
  impact["session"] = session;
  impact["actionId"] = std::string(32, 'a');
  impact["targetId"] = cowId;
  auto bad = impact;
  bad["version"] = 2;
  send(bad);
  bad = impact;
  bad["actionId"] = "bad-id";
  send(bad);
  bad = impact;
  bad["targetId"] = 0xfffffff0u;
  send(bad);
  REQUIRE(listener->requests.empty());
  REQUIRE_FALSE(world.LookupFormByIdNoLoad(0xfffffff0u));
  RawMessageData raw;
  raw.userId = testUserId;
  HitMessage legacy;
  legacy.data.aggressor = 0x14;
  legacy.data.target = cowId;
  legacy.data.source = 0x1f4;
  server.GetActionListener().OnHit(raw, legacy);
  REQUIRE(listener->requests.empty());

  const auto health = cow.GetActorValues().healthPercentage;
  send(impact);
  REQUIRE(listener->requests.size() == 1);
  REQUIRE(listener->result.at("state") == "ISSUED");
  const auto op = listener->result.at("operationId").get<std::string>();
  send(impact);
  REQUIRE(listener->requests.at(0) == listener->requests.at(1));
  REQUIRE(listener->result.at("duplicate") == true);
  REQUIRE(listener->result.at("operationId") == op);
  WaitForSaved(server, player);
  REQUIRE(SrpOriginalImpact::Apply(server, testUserId, op).at("state") == "TARGET_SAVE_PENDING");
  WaitForSaved(server, cow);
  const auto after = cow.GetActorValues().healthPercentage;
  REQUIRE(after < health);
  // Read the exact persisted boundary before attacker acknowledgement.
  std::optional<MpChangeForm> savedTarget, savedAttacker;
  database->Iterate([&](const MpChangeForm& form) {
    if (form.formDesc == cow.GetChangeForm().formDesc) savedTarget = form;
    if (form.formDesc == player.GetChangeForm().formDesc) savedAttacker = form;
  }, std::vector<FormDesc>{cow.GetChangeForm().formDesc, player.GetChangeForm().formDesc});
  REQUIRE(savedTarget.has_value());
  REQUIRE(savedAttacker.has_value());
  REQUIRE(savedTarget->actorValues.healthPercentage == after);
  REQUIRE(savedTarget->dynamicFields.GetAsJson().at(SrpOriginalImpact::kProperty)
    .at("operations").contains(op));

  auto& restarted = GetPartOne();
  AttachStorage(restarted);
  restarted.worldState.LoadChangeForm(*savedTarget, restarted.CreateFormCallbacks());
  restarted.worldState.LoadChangeForm(*savedAttacker, restarted.CreateFormCallbacks());
  restarted.worldState.npcEnabled = true;
  auto& restoredTarget = restarted.worldState.GetFormAt<MpActor>(cowId);
  restarted.worldState.npcEnabled = false;
  auto& restoredPlayer = restarted.worldState.GetFormAt<MpActor>(playerId);
  restarted.serverState.Connect(testUserId, "test-guid-1");
  restarted.serverState.actorsMap.Set(testUserId, &restoredPlayer);
  REQUIRE(restoredTarget.GetActorValues().healthPercentage == after);
  const auto replayed = SrpOriginalImpact::Apply(restarted, testUserId, op);
  REQUIRE(replayed.at("duplicate") == true);
  if (replayed.at("state") == "TARGET_SAVE_PENDING") {
    WaitForSaved(restarted, restoredTarget);
    REQUIRE(SrpOriginalImpact::Apply(restarted, testUserId, op).at("state") == "APPLIED");
  } else {
    REQUIRE(replayed.at("state") == "APPLIED");
  }
  REQUIRE(restoredTarget.GetActorValues().healthPercentage == after);
  const auto replayListener = std::make_shared<ImpactListener>();
  restarted.AddListener(replayListener);
  replayListener->server = &restarted;
  CustomPacketMessage oldImpact;
  oldImpact.contentJsonDump = impact.dump();
  restarted.GetActionListener().OnCustomPacket(raw, oldImpact);
  REQUIRE(replayListener->requests.empty());
  replayListener->server = nullptr;

  server.serverState.Disconnect(testUserId);
  server.serverState.Connect(testUserId, "test-guid-1");
  server.serverState.actorsMap.Set(testUserId, &player);
  send(impact);
  REQUIRE(listener->requests.size() == 2);
  REQUIRE(negotiate() != session);
  REQUIRE(SrpOriginalImpact::Apply(server, testUserId, op).at("state") == "APPLIED");
  REQUIRE(cow.GetActorValues().healthPercentage == after);
  WaitForSaved(server, player);

  const auto current = negotiate();
  auto renewed = impact;
  renewed["session"] = current;
  send(renewed);
  REQUIRE(listener->requests.size() == 3);
  REQUIRE(listener->result.at("operationId") == op);
  REQUIRE(listener->result.at("state") == "APPLIED");
  server.SetUserActor(testUserId, 0);
  server.SetUserActor(testUserId, playerId);
  REQUIRE(negotiate() != current);
  impact["session"] = current;
  send(impact);
  REQUIRE(listener->requests.size() == 3);
  REQUIRE(cow.GetActorValues().healthPercentage == after);
  WaitForSaved(server, player);
  std::this_thread::sleep_for(std::chrono::milliseconds(1100));
  renewed["session"] = negotiate();
  renewed["actionId"] = std::string(32, 'b');
  send(renewed);
  REQUIRE(listener->requests.size() == 4);
  REQUIRE(listener->result.at("state") == "ISSUED");
  REQUIRE(listener->result.at("operationId") != op);
  listener->server = nullptr;
}

TEST_CASE("Original melee reconciles durable target damage after SIGKILL before acknowledgement",
          "[srp-r46-crash][crash]")
{
  using Json = nlohmann::json;
  const auto path = std::filesystem::temp_directory_path() /
    ("srp-impact-crash-" + std::to_string(getpid()));
  int signal[2];
  REQUIRE(pipe(signal) == 0);
  const auto child = fork();
  REQUIRE(child >= 0);
  if (child == 0) {
    close(signal[0]);
    try {
      auto& server = GetPartOne();
      AttachStorage(server, path);
      auto& target = SetupCow(server);
      auto& attacker = SetupPlayer(server, target);
      auto& world = server.worldState;
      const auto desc = [&](uint32_t id) {
        return FormDesc::FromFormId(id, world.espmFiles).ToString();
      };
      const auto cell = target.GetCellOrWorld().ToFormId(world.espmFiles);
      world.InstallSrpCollisionScene(CreateSyntheticScene(cell, desc(target.GetBaseId()),
        desc(target.GetRaceId()), target.GetPos(), desc(attacker.GetBaseId()),
        desc(attacker.GetRaceId())), AuditConfig(cell, true));
      const auto before = target.GetActorValues().healthPercentage;
      const auto id = SrpOriginalImpact::Begin(server, testUserId, cowId,
        "hit-0123456789abcdef0123456789abcdef").at("operationId").get<std::string>();
      WaitForSaved(server, attacker);
      REQUIRE(SrpOriginalImpact::Apply(server, testUserId, id).at("state") == "TARGET_SAVE_PENDING");
      WaitForSaved(server, target);
      REQUIRE(target.GetActorValues().healthPercentage < before);
      const char ready = 'R';
      if (write(signal[1], &ready, 1) != 1)
        _exit(2);
      for (;;)
        pause();
    } catch (...) {
      _exit(3);
    }
  }
  close(signal[1]);
  pollfd descriptor{signal[0], POLLIN, 0};
  char phase = 0;
  if (poll(&descriptor, 1, 45000) > 0)
    read(signal[0], &phase, 1);
  close(signal[0]);
  kill(child, SIGKILL);
  int status = 0;
  REQUIRE(waitpid(child, &status, 0) == child);
  REQUIRE(phase == 'R');
  REQUIRE(WIFSIGNALED(status));
  REQUIRE(WTERMSIG(status) == SIGKILL);

  auto& restarted = GetPartOne();
  const auto database = AttachStorage(restarted, path);
  database->Iterate([&](const MpChangeForm& form) {
    restarted.worldState.LoadChangeForm(form, restarted.CreateFormCallbacks());
  }, std::nullopt);
  restarted.worldState.npcEnabled = true;
  auto& target = restarted.worldState.GetFormAt<MpActor>(cowId);
  restarted.worldState.npcEnabled = false;
  auto& attacker = restarted.worldState.GetFormAt<MpActor>(playerId);
  restarted.serverState.Connect(testUserId, "test-guid-after-crash");
  restarted.serverState.actorsMap.Set(testUserId, &attacker);
  const auto ledger = attacker.GetDynamicFields().GetAsJson().at(SrpOriginalImpact::kProperty);
  const auto id = ledger.at("pending").get<std::string>();
  REQUIRE(ledger.at("operations").at(id).at("state") == "ISSUED");
  REQUIRE(target.GetDynamicFields().GetAsJson().at(SrpOriginalImpact::kProperty)
    .at("operations").contains(id));
  const auto health = target.GetActorValues().healthPercentage;
  const auto replay = SrpOriginalImpact::Apply(restarted, testUserId, id);
  REQUIRE(replay.at("duplicate") == true);
  if (replay.at("state") == "TARGET_SAVE_PENDING")
    WaitForSaved(restarted, target);
  REQUIRE(SrpOriginalImpact::Apply(restarted, testUserId, id).at("state") == "APPLIED");
  WaitForSaved(restarted, attacker);
  REQUIRE(target.GetActorValues().healthPercentage == health);

  // Reload a fully acknowledged outbox too. Its in-process generation starts
  // at zero, so retry must schedule a save before returning saved=true.
  std::optional<MpChangeForm> terminal;
  database->Iterate([&](const MpChangeForm& form) {
    if (form.formDesc == attacker.GetChangeForm().formDesc) terminal = form;
  }, std::vector<FormDesc>{attacker.GetChangeForm().formDesc});
  REQUIRE(terminal.has_value());
  auto& afterAck = GetPartOne();
  AttachStorage(afterAck, path);
  afterAck.worldState.LoadChangeForm(*terminal, afterAck.CreateFormCallbacks());
  auto& reloadedAttacker = afterAck.worldState.GetFormAt<MpActor>(playerId);
  afterAck.serverState.Connect(testUserId, "test-guid-after-ack");
  afterAck.serverState.actorsMap.Set(testUserId, &reloadedAttacker);
  REQUIRE(afterAck.worldState.GetChangeFormGeneration(playerId) == 0);
  const auto acknowledged = SrpOriginalImpact::Apply(afterAck, testUserId, id);
  REQUIRE(acknowledged.at("state") == "APPLIED");
  REQUIRE(acknowledged.at("saved") == false);
  REQUIRE(afterAck.worldState.GetChangeFormGeneration(playerId) > 0);
  WaitForSaved(afterAck, reloadedAttacker);
  REQUIRE(SrpOriginalImpact::Apply(afterAck, testUserId, id).at("saved") == true);
  REQUIRE(SrpOriginalImpact::Cancel(afterAck, testUserId, id).at("saved") == true);
  REQUIRE_FALSE(afterAck.worldState.LookupFormByIdNoLoad(cowId));
}

TEST_CASE("Impact verifies complete collision scene, rate limit, single hit, and replay resistance", "[srp-original-impact]")
{
  auto& w = GetPartOne();
  AttachStorage(w);
  auto& cow = SetupCow(w);
  auto& player = SetupPlayer(w, cow);
  Isolate(w, cow, player);

  const uint32_t cellId = cow.GetCellOrWorld().ToFormId(w.worldState.espmFiles);
  const auto baseDesc = FormDesc::FromFormId(cow.GetBaseId(), w.worldState.espmFiles).ToString();
  const auto raceDesc = FormDesc::FromFormId(cow.GetRaceId(), w.worldState.espmFiles).ToString();

  const auto playerBase = FormDesc::FromFormId(player.GetBaseId(), w.worldState.espmFiles).ToString();
  const auto playerRace = FormDesc::FromFormId(player.GetRaceId(), w.worldState.espmFiles).ToString();
  auto scene = CreateSyntheticScene(cellId, baseDesc, raceDesc, cow.GetPos(),
                                    playerBase, playerRace);
  w.worldState.InstallSrpCollisionScene(scene, AuditConfig(cellId, true));

  // 1. Begin valid attack
  auto beginResult = SrpOriginalImpact::Begin(w, testUserId, cowId, "req-valid-1");
  REQUIRE(beginResult.at("duplicate").get<bool>() == false);
  REQUIRE(beginResult.at("state").get<std::string>() == "ISSUED");
  const std::string opId = beginResult.at("operationId").get<std::string>();
  REQUIRE(!opId.empty());

  // 2. Idempotent replay of same requestId returns duplicate without creating a new operation
  auto duplicateBegin = SrpOriginalImpact::Begin(w, testUserId, cowId, "req-valid-1");
  REQUIRE(duplicateBegin.at("duplicate").get<bool>() == true);
  REQUIRE(duplicateBegin.at("operationId").get<std::string>() == opId);

  // 3. Rate limiting prevents second swing while pending
  REQUIRE_THROWS_WITH(
    SrpOriginalImpact::Begin(w, testUserId, cowId, "req-second-swing"),
    "SRP_IMPACT_PENDING_SWING_EXISTS"
  );

  // Wait for save of the outbox intent
  WaitForSaved(w, player);

  // 4. Apply damage
  const float beforeHealth = cow.GetActorValues().healthPercentage;
  auto applyResult = SrpOriginalImpact::Apply(w, testUserId, opId);
  REQUIRE(applyResult.at("state").get<std::string>() == "TARGET_SAVE_PENDING");
  REQUIRE(applyResult.at("saved").get<bool>() == false);

  WaitForSaved(w, cow);

  // 5. Re-apply / reconcile: acknowledged and target health decreased
  auto reconcileResult = SrpOriginalImpact::Apply(w, testUserId, opId);
  REQUIRE(reconcileResult.at("state").get<std::string>() == "APPLIED");
  REQUIRE(reconcileResult.at("duplicate").get<bool>() == true);

  const float afterHealth = cow.GetActorValues().healthPercentage;
  REQUIRE(afterHealth < beforeHealth);

  // 6. Inspect returns state correctly
  auto targetInspect = SrpOriginalImpact::Inspect(cow);
  REQUIRE(targetInspect.at("present").get<bool>() == true);
  REQUIRE(targetInspect.at("legacyHitAuthority").get<bool>() == false);
  REQUIRE(targetInspect.at("lootEnabled").get<bool>() == false);
  REQUIRE(targetInspect.at("xpEnabled").get<bool>() == false);
}

TEST_CASE("Impact fails closed when target has no collision shape proxy", "[srp-original-impact]")
{
  auto& w = GetPartOne();
  AttachStorage(w);
  auto& cow = SetupCow(w);
  auto& player = SetupPlayer(w, cow);
  Isolate(w, cow, player);

  const uint32_t cellId = cow.GetCellOrWorld().ToFormId(w.worldState.espmFiles);
  // Install scene with a different base shape so target lookup returns empty
  const auto playerBase = FormDesc::FromFormId(player.GetBaseId(), w.worldState.espmFiles).ToString();
  const auto playerRace = FormDesc::FromFormId(player.GetRaceId(), w.worldState.espmFiles).ToString();
  auto scene = CreateSyntheticScene(cellId, "OtherBase:Skyrim.esm", "OtherRace:Skyrim.esm",
                                    cow.GetPos(), playerBase, playerRace);
  w.worldState.InstallSrpCollisionScene(scene, AuditConfig(cellId, true));

  REQUIRE_THROWS_WITH(
    SrpOriginalImpact::Begin(w, testUserId, cowId, "req-no-shape-1"),
    "SRP_IMPACT_SERVER_FRAME_UNAVAILABLE"
  );
}

TEST_CASE("Impact fails closed when attacker is turned away or out of melee reach", "[srp-original-impact]")
{
  auto& w = GetPartOne();
  AttachStorage(w);
  auto& cow = SetupCow(w);
  auto& player = SetupPlayer(w, cow);
  Isolate(w, cow, player);

  const uint32_t cellId = cow.GetCellOrWorld().ToFormId(w.worldState.espmFiles);
  const auto baseDesc = FormDesc::FromFormId(cow.GetBaseId(), w.worldState.espmFiles).ToString();
  const auto raceDesc = FormDesc::FromFormId(cow.GetRaceId(), w.worldState.espmFiles).ToString();
  const auto playerBase = FormDesc::FromFormId(player.GetBaseId(), w.worldState.espmFiles).ToString();
  const auto playerRace = FormDesc::FromFormId(player.GetRaceId(), w.worldState.espmFiles).ToString();
  auto scene = CreateSyntheticScene(cellId, baseDesc, raceDesc, cow.GetPos(),
                                    playerBase, playerRace);
  w.worldState.InstallSrpCollisionScene(scene, AuditConfig(cellId, true));

  // Turn attacker 180 degrees away from target (facing south instead of north)
  player.SetAngle({0, 0, 180.0f});
  // The turn is itself a durable change: admission refuses an unsaved attacker
  // before it ever evaluates geometry, so the rejection under test would be
  // masked by SRP_IMPACT_PRIOR_SAVE_PENDING.
  WaitForSaved(w, player);

  REQUIRE_THROWS_WITH(
    SrpOriginalImpact::Begin(w, testUserId, cowId, "req-miss-1"),
    "SRP_IMPACT_SERVER_SEGMENT_MISSES_TARGET"
  );
}

TEST_CASE("Reviewed dynamic hits require server contact and a current host",
          "[srp-dynamic-melee][espm]")
{
  auto& server = GetPartOne();
  server.SetDamageFormula(std::make_unique<TES5DamageFormula>());
  auto& world = server.worldState;
  const nlohmann::json config = {
    {"schemaVersion", 1}, {"enabled", true},
    {"limits", {{"total", 3}, {"fauna", 2}, {"other", 2}, {"perRegion", 3}}},
    {"sources", nlohmann::json::array({{
      {"plugin", "Skyrim.esm"}, {"category", "fauna"},
      {"references", nlohmann::json::array()}}})},
    {"faunaBases", {"23abe:Skyrim.esm"}},
    {"reviewedMeleeBases", {"23abe:Skyrim.esm"}}
  };
  auto invalid = config;
  invalid["reviewedMeleeBases"] = {"23abf:Skyrim.esm"};
  REQUIRE_THROWS_WITH(world.ConfigureSrpNpcAdmission(invalid),
                      "SRP_NPC_ADMISSION_MELEE_BASE_INVALID");
  world.ConfigureSrpNpcAdmission(config);
  AttachStorage(server);
  constexpr uint32_t ref = 0xff000095, cell = 0x3c, dagger = 0x1397e;
  world.AddForm(std::make_unique<MpActor>(
    LocationalData{{0, 0, 0}, {0, 0, 180}, FormDesc::Tamriel()},
    server.CreateFormCallbacks(), 0x23abe), ref);
  auto& npc = world.GetFormAt<MpActor>(ref);
  WaitForSaved(server, npc);
  auto& player = SetupPlayer(server, npc);
  REQUIRE(world.RequiresSrpDynamicMeleeProof(npc));
  REQUIRE_FALSE(world.RequiresSrpDynamicMeleeProof(player));
  REQUIRE_FALSE(SrpOriginalLifecycle::Managed(npc));
  const auto desc = [&](uint32_t id) {
    return FormDesc::FromFormId(id, world.espmFiles).ToString();
  };
  const auto scene = [&](bool wall) {
    return CreateSyntheticScene(cell, desc(npc.GetBaseId()), desc(npc.GetRaceId()),
      npc.GetPos(), desc(player.GetBaseId()), desc(player.GetRaceId()), false, wall);
  };
  player.AddItem(dagger, 1);
  Inventory::ExtraData worn;
  worn.worn_ = true;
  Equipment equipment;
  equipment.inv.entries.emplace_back(dagger, 1, worn);
  player.SetEquipment(equipment);
  const auto npcHealth = [&] { return npc.GetActorValues().healthPercentage; };
  const auto playerHealth = [&] { return player.GetActorValues().healthPercentage; };
  const auto startNpc = npcHealth(), startPlayer = playerHealth();
  const auto hit = [&](uint32_t attacker, uint32_t target, uint32_t source) {
    HitMessage message;
    message.data.aggressor = attacker;
    message.data.target = target;
    message.data.source = source;
    RawMessageData raw;
    raw.userId = testUserId;
    server.GetActionListener().OnHit(raw, message);
  };
  constexpr uint32_t coldRef = 0xff0000a0;
  MpChangeForm cold;
  cold.recType = MpChangeForm::ACHR;
  cold.formDesc = FormDesc::FromFormId(coldRef, world.espmFiles);
  cold.baseDesc = FormDesc::FromString(desc(npc.GetBaseId()));
  cold.worldOrCellDesc = FormDesc::Tamriel();
  cold.position = {81920, 0, 0};
  cold.profileId = -1;
  world.LoadChangeForm(cold, FormCallbacks::DoNothing());
  REQUIRE(world.InspectSrpNpcAdmission().at("deferredDynamicNpcs") == 1);
  hit(0x14, coldRef, dagger);
  REQUIRE_FALSE(world.LookupFormByIdNoLoad(coldRef));
  REQUIRE(world.InspectSrpNpcAdmission().at("deferredDynamicNpcs") == 1);
  hit(0x14, ref, dagger);
  REQUIRE(npcHealth() == startNpc);
  world.InstallSrpCollisionScene(scene(true), AuditConfig(cell, true));
  hit(0x14, ref, dagger);
  REQUIRE(npcHealth() == startNpc);
  auto incomplete = AuditConfig(cell, true);
  incomplete["physicalBaselineVerified"] = true;
  incomplete["dynamicAudit"]["untrackedPhysics"] = {0x7546f};
  world.InstallSrpCollisionScene(scene(false), incomplete);
  hit(0x14, ref, dagger);
  REQUIRE(npcHealth() == startNpc);
  world.InstallSrpCollisionScene(scene(false), AuditConfig(cell, true));
  hit(0x14, ref, 0x1f4);
  REQUIRE(npcHealth() == startNpc);
  hit(0x14, ref, dagger);
  REQUIRE(npcHealth() < startNpc);
  REQUIRE_FALSE(npc.IsDead());
  const auto after = npcHealth();
  hit(0x14, ref, dagger);
  REQUIRE(npcHealth() == after);
  hit(ref, 0x14, 0x1f4);
  REQUIRE(playerHealth() == startPlayer);
  MpObjectReference::Subscribe(&npc, &player);
  world.hosters[ref] = playerId;
  world.InstallSrpCollisionScene(scene(true), AuditConfig(cell, true));
  hit(ref, 0x14, 0x1f4);
  REQUIRE(playerHealth() == startPlayer);
  world.InstallSrpCollisionScene(scene(false), AuditConfig(cell, true));
  REQUIRE_NOTHROW(SrpOriginalImpact::RequireDynamicMeleeContact(
    world, npc, player, 0x1f4));
  REQUIRE(npc.GetListeners().count(&player) == 1);
  REQUIRE(server.serverState.UserByActor(&npc) == Networking::InvalidUserId);
  hit(ref, 0x14, 0x1f4);
  REQUIRE(playerHealth() < startPlayer);
}

TEST_CASE("Impact refuses a dynamic snapshot the audit does not cover", "[srp-original-impact]")
{
  auto& w = GetPartOne();
  AttachStorage(w);
  auto& cow = SetupCow(w);
  auto& player = SetupPlayer(w, cow);
  Isolate(w, cow, player);

  const uint32_t cellId = cow.GetCellOrWorld().ToFormId(w.worldState.espmFiles);
  const auto baseDesc = FormDesc::FromFormId(cow.GetBaseId(), w.worldState.espmFiles).ToString();
  const auto raceDesc = FormDesc::FromFormId(cow.GetRaceId(), w.worldState.espmFiles).ToString();
  const auto playerBase = FormDesc::FromFormId(player.GetBaseId(), w.worldState.espmFiles).ToString();
  const auto playerRace = FormDesc::FromFormId(player.GetRaceId(), w.worldState.espmFiles).ToString();
  auto scene = CreateSyntheticScene(cellId, baseDesc, raceDesc, cow.GetPos(),
                                    playerBase, playerRace);

  // 1. No audit at all: the server must not claim an empty dynamic snapshot.
  w.worldState.InstallSrpCollisionScene(scene);
  REQUIRE_THROWS_WITH(
    SrpOriginalImpact::Begin(w, testUserId, cowId, "req-no-audit-1"),
    "SRP_IMPACT_SERVER_FRAME_UNAVAILABLE"
  );

  // 2. An audit for a different cell does not cover this one.
  w.worldState.InstallSrpCollisionScene(scene, AuditConfig(cellId + 1, true));
  REQUIRE_THROWS_WITH(
    SrpOriginalImpact::Begin(w, testUserId, cowId, "req-wrong-cell-1"),
    "SRP_IMPACT_SERVER_FRAME_UNAVAILABLE"
  );

  // 3. An audit that does not claim completeness is not enough either.
  w.worldState.InstallSrpCollisionScene(scene, AuditConfig(cellId, false));
  REQUIRE_THROWS_WITH(
    SrpOriginalImpact::Begin(w, testUserId, cowId, "req-incomplete-1"),
    "SRP_IMPACT_SERVER_FRAME_UNAVAILABLE"
  );

  // 4. A listed blocker that is live can open or move, so the audited baseline
  //    no longer describes the current state.
  w.worldState.InstallSrpCollisionScene(scene, AuditConfig(cellId, true, {cowId}));
  REQUIRE_THROWS_WITH(
    SrpOriginalImpact::Begin(w, testUserId, cowId, "req-live-blocker-1"),
    "SRP_IMPACT_SERVER_FRAME_UNAVAILABLE"
  );
}

TEST_CASE("Original impact binds third actor envelopes and refuses stale or incomplete LOS",
          "[srp-original-impact]")
{
  auto& server = GetPartOne();
  AttachStorage(server);
  auto& cow = SetupCow(server);
  auto& player = SetupPlayer(server, cow);
  Isolate(server, cow, player);
  auto& world = server.worldState;
  const auto cell = cow.GetCellOrWorld().ToFormId(world.espmFiles);
  const auto descriptor = [&](uint32_t id) {
    return FormDesc::FromFormId(id, world.espmFiles).ToString();
  };
  const auto install = [&](bool withBounds, const nlohmann::json& audit) {
    world.InstallSrpCollisionScene(CreateSyntheticScene(cell, descriptor(cow.GetBaseId()),
      descriptor(cow.GetRaceId()), cow.GetPos(), descriptor(player.GetBaseId()),
      descriptor(player.GetRaceId()), withBounds), audit);
  };
  auto audit = AuditConfig(cell, true);
  const uint32_t bystanderId = 0xff000092;
  auto blockedPosition = cow.GetPos();
  blockedPosition.y -= 30;
  server.CreateActor(bystanderId, blockedPosition, 0, cell, 9192);
  auto& bystander = world.GetFormAt<MpActor>(bystanderId);
  install(false, audit);
  REQUIRE_FALSE(world.CaptureSrpOriginalImpactFrame(player, cow).has_value());
  install(true, audit);
  const auto blocked = world.CaptureSrpOriginalImpactFrame(player, cow);
  REQUIRE(blocked.has_value());
  REQUIRE(blocked->dynamics.unresolvedBlockers.size() == 1);
  REQUIRE_THROWS_WITH(SrpOriginalImpact::Begin(server, testUserId, cowId, "bystander-blocks"),
                      "SRP_IMPACT_SERVER_COLLISION_NOT_CLEAR");

  auto clearPosition = blockedPosition;
  clearPosition.x += 150;
  bystander.SetPos(clearPosition);
  const auto clear = world.CaptureSrpOriginalImpactFrame(player, cow);
  REQUIRE(clear.has_value());
  REQUIRE(clear->dynamicStateDigest != blocked->dynamicStateDigest);
  const auto issued = SrpOriginalImpact::Begin(server, testUserId, cowId, "bystander-clear");
  WaitForSaved(server, player);
  // The move is outside the attack segment but still invalidates the snapshot.
  clearPosition.x += 5;
  bystander.SetPos(clearPosition);
  REQUIRE_THROWS_WITH(SrpOriginalImpact::Apply(server, testUserId, issued.at("operationId").get<std::string>()),
                      "SRP_IMPACT_FRAME_CHANGED");
  const auto health = cow.GetActorValues().healthPercentage;
  REQUIRE(health > 0.f);
  const auto beforeTransition = bystander.GetChangeForm();
  bystander.SetPropertyValueDump("private.srpTransformationV1",
    R"({"schemaVersion":1,"fenced":true})", false, false);
  REQUIRE_FALSE(world.CaptureSrpOriginalImpactFrame(player, cow).has_value());
  bystander.ApplyChangeForm(beforeTransition);
  REQUIRE(world.CaptureSrpOriginalImpactFrame(player, cow).has_value());
  audit["physicalBaselineVerified"] = true;
  audit["dynamicAudit"]["untrackedPhysics"] = nlohmann::json::array({0x7546f});
  install(true, audit);
  REQUIRE_FALSE(world.CaptureSrpOriginalImpactFrame(player, cow).has_value());
  REQUIRE(cow.GetActorValues().healthPercentage == health);
  server.DestroyActor(bystanderId);
  world.InstallSrpCollisionScene(nullptr);
  server.DestroyActor(playerId);
  server.serverState.Disconnect(testUserId);
}

TEST_CASE("Impact refuses an attacker whose collision proxy is unknown", "[srp-original-impact]")
{
  auto& w = GetPartOne();
  AttachStorage(w);
  auto& cow = SetupCow(w);
  auto& player = SetupPlayer(w, cow);
  Isolate(w, cow, player);

  const uint32_t cellId = cow.GetCellOrWorld().ToFormId(w.worldState.espmFiles);
  const auto baseDesc = FormDesc::FromFormId(cow.GetBaseId(), w.worldState.espmFiles).ToString();
  const auto raceDesc = FormDesc::FromFormId(cow.GetRaceId(), w.worldState.espmFiles).ToString();
  const auto playerRace = FormDesc::FromFormId(player.GetRaceId(), w.worldState.espmFiles).ToString();
  // The scene knows the target but has no proxy for this attacker, so the grip
  // height cannot be derived from anything real. Admission must refuse instead
  // of falling back to an invented offset.
  auto scene = CreateSyntheticScene(cellId, baseDesc, raceDesc, cow.GetPos(),
                                    "AbsentBase:Skyrim.esm", playerRace);
  w.worldState.InstallSrpCollisionScene(scene, AuditConfig(cellId, true));
  REQUIRE_THROWS_WITH(
    SrpOriginalImpact::Begin(w, testUserId, cowId, "req-no-attacker-proxy-1"),
    "SRP_IMPACT_ATTACKER_PROXY_REQUIRED"
  );
}
TEST_CASE("Impact refuses a target in another cell", "[srp-original-impact]")
{
  auto& w = GetPartOne();
  AttachStorage(w);
  auto& cow = SetupCow(w);
  auto& player = SetupPlayer(w, cow);
  Isolate(w, cow, player);

  const uint32_t cellId = cow.GetCellOrWorld().ToFormId(w.worldState.espmFiles);
  const auto baseDesc = FormDesc::FromFormId(cow.GetBaseId(), w.worldState.espmFiles).ToString();
  const auto raceDesc = FormDesc::FromFormId(cow.GetRaceId(), w.worldState.espmFiles).ToString();
  const auto playerBase = FormDesc::FromFormId(player.GetBaseId(), w.worldState.espmFiles).ToString();
  const auto playerRace = FormDesc::FromFormId(player.GetRaceId(), w.worldState.espmFiles).ToString();
  auto scene = CreateSyntheticScene(cellId, baseDesc, raceDesc, cow.GetPos(),
                                    playerBase, playerRace);
  w.worldState.InstallSrpCollisionScene(scene, AuditConfig(cellId, true));

  // WhiterunDragonsreach: a real interior, so the form id resolves, but a
  // different space from the target's. A snapshot describes one cell only.
  const uint32_t otherCell = 0x000165a3;
  REQUIRE(otherCell != cellId);
  player.SetCellOrWorld(FormDesc::FromFormId(otherCell, w.worldState.espmFiles));
  WaitForSaved(w, player);
  REQUIRE(player.GetCellOrWorld().ToFormId(w.worldState.espmFiles) == otherCell);

  REQUIRE_THROWS_WITH(
    SrpOriginalImpact::Begin(w, testUserId, cowId, "req-other-cell-1"),
    "SRP_IMPACT_SERVER_FRAME_UNAVAILABLE"
  );
}
namespace {
constexpr uint32_t secondPlayerId = 0xff000092;
constexpr Networking::UserId secondUserId = 8;

MpActor& SetupSecondPlayer(PartOne& w, const MpActor& target)
{
  w.serverState.Connect(secondUserId, "test-guid-2");
  NiPoint3 pos = target.GetPos();
  pos.y += 50.0f;
  const uint32_t cellId = target.GetCellOrWorld().ToFormId(w.worldState.espmFiles);
  w.CreateActor(secondPlayerId, pos, 180.0f, cellId, 9192);
  auto& player = w.worldState.GetFormAt<MpActor>(secondPlayerId);
  w.serverState.actorsMap.Set(secondUserId, &player);
  player.SetPropertyValueDump("srpActiveCharacterId", "\"combat-PC2\"", false, false);
  nlohmann::json proj = {
    {"schemaVersion", 2}, {"rulesVersion", "SRP-PROGRESSION-A1"}, {"revision", 1},
    {"profileId", "9192"}, {"characterId", "combat-PC2"}, {"level", 10},
    {"attributes", {{"vigor", 0}, {"strength", 5}, {"dexterity", 0}, {"intelligence", 0}, {"willpower", 0}, {"constitution", 5}}}
  };
  player.SetPropertyValueDump("srpProgression", proj.dump(), false, false);
  player.ApplySrpActorStats(proj);
  WaitForSaved(w, player);
  return player;
}
}

TEST_CASE("Impact serialises two attackers without double damage", "[srp-original-impact]")
{
  auto& w = GetPartOne();
  AttachStorage(w);
  auto& cow = SetupCow(w);
  auto& player = SetupPlayer(w, cow);
  Isolate(w, cow, player);
  auto& second = SetupSecondPlayer(w, cow);
  ClearImpactState(second);
  WaitForSaved(w, second);

  const auto gen1 = [&] { return w.worldState.GetChangeFormGeneration(player.GetFormId()); };
  const auto gen2 = [&] { return w.worldState.GetChangeFormGeneration(second.GetFormId()); };
  const auto genT = [&] { return w.worldState.GetChangeFormGeneration(cowId); };

  const uint32_t cellId = cow.GetCellOrWorld().ToFormId(w.worldState.espmFiles);
  const auto baseDesc = FormDesc::FromFormId(cow.GetBaseId(), w.worldState.espmFiles).ToString();
  const auto raceDesc = FormDesc::FromFormId(cow.GetRaceId(), w.worldState.espmFiles).ToString();
  const auto playerBase = FormDesc::FromFormId(player.GetBaseId(), w.worldState.espmFiles).ToString();
  const auto playerRace = FormDesc::FromFormId(player.GetRaceId(), w.worldState.espmFiles).ToString();
  auto scene = CreateSyntheticScene(cellId, baseDesc, raceDesc, cow.GetPos(),
                                    playerBase, playerRace, true);
  w.worldState.InstallSrpCollisionScene(scene, AuditConfig(cellId, true));

  WARN("STEP 0 gens a1=" << gen1() << " a2=" << gen2() << " target=" << genT());
  const auto first = SrpOriginalImpact::Begin(w, testUserId, cowId, "req-two-a");
  WARN("STEP 1 after beginA gens a1=" << gen1() << " a2=" << gen2() << " target=" << genT());
  const auto other = SrpOriginalImpact::Begin(w, secondUserId, cowId, "req-two-b");
  WARN("STEP 2 after beginB gens a1=" << gen1() << " a2=" << gen2() << " target=" << genT());
  REQUIRE(first.at("state").get<std::string>() == "ISSUED");
  REQUIRE(other.at("state").get<std::string>() == "ISSUED");
  const std::string opA = first.at("operationId").get<std::string>();
  const std::string opB = other.at("operationId").get<std::string>();
  REQUIRE(opA != opB);

  // Both intents must be durable before either can be applied: Apply refuses
  // an attacker whose intent write is still pending.
  WaitForSaved(w, player);
  WaitForSaved(w, second);

  const float before = cow.GetActorValues().healthPercentage;
  const auto appliedA = SrpOriginalImpact::Apply(w, testUserId, opA);
  WARN("STEP 3 after applyA gens a1=" << gen1() << " a2=" << gen2() << " target=" << genT());
  REQUIRE(appliedA.at("state").get<std::string>() == "TARGET_SAVE_PENDING");
  WaitForSaved(w, cow);
  const float afterA = cow.GetActorValues().healthPercentage;
  WARN("STEP 4 after save gens a1=" << gen1() << " a2=" << gen2() << " target=" << genT());
  REQUIRE(afterA < before);

  REQUIRE_THROWS_WITH(SrpOriginalImpact::Apply(w, secondUserId, opB),
                      "SRP_IMPACT_GENERATION_CHANGED");
  REQUIRE(cow.GetActorValues().healthPercentage == afterA);

  const auto cancelled = SrpOriginalImpact::Cancel(w, secondUserId, opB);
  REQUIRE(cancelled.at("state").get<std::string>() == "CANCELLED");
  REQUIRE(cow.GetActorValues().healthPercentage == afterA);
}
TEST_CASE("Lethal impact records exactly one authoritative death", "[srp-original-impact]")
{
  auto& w = GetPartOne();
  AttachStorage(w);
  auto& cow = SetupCow(w);
  auto& player = SetupPlayer(w, cow);
  Isolate(w, cow, player);

  const uint32_t cellId = cow.GetCellOrWorld().ToFormId(w.worldState.espmFiles);
  const auto baseDesc = FormDesc::FromFormId(cow.GetBaseId(), w.worldState.espmFiles).ToString();
  const auto raceDesc = FormDesc::FromFormId(cow.GetRaceId(), w.worldState.espmFiles).ToString();
  const auto playerBase = FormDesc::FromFormId(player.GetBaseId(), w.worldState.espmFiles).ToString();
  const auto playerRace = FormDesc::FromFormId(player.GetRaceId(), w.worldState.espmFiles).ToString();
  auto scene = CreateSyntheticScene(cellId, baseDesc, raceDesc, cow.GetPos(),
                                    playerBase, playerRace);
  w.worldState.InstallSrpCollisionScene(scene, AuditConfig(cellId, true));

  // Leave a sliver so one authorised hit is lethal, without going through
  // SetPercentages(0) (that path calls Kill internally).
  auto health = cow.GetActorValues();
  health.healthPercentage = 0.02f;
  cow.SetPercentages(health, nullptr);
  WaitForSaved(w, cow);

  const auto begun = SrpOriginalImpact::Begin(w, testUserId, cowId, "req-lethal-1");
  REQUIRE(begun.at("state").get<std::string>() == "ISSUED");
  const std::string opId = begun.at("operationId").get<std::string>();
  WaitForSaved(w, player);

  const auto applied = SrpOriginalImpact::Apply(w, testUserId, opId);
  REQUIRE(applied.at("lethal").get<bool>() == true);
  REQUIRE(cow.IsDead());

  const auto death = SrpOriginalLifecycle::Inspect(cow);
  REQUIRE(death.at("state").get<std::string>() == "DEAD");
  const auto& life = death.at("lifecycle");
  REQUIRE(life.at("schemaVersion").get<int>() == 2);
  REQUIRE(life.at("state").get<std::string>() == "DEAD");
  REQUIRE(life.at("death").at("operationId").get<std::string>() == opId);
  REQUIRE(life.at("death").at("source").get<std::string>() == "SRP_ORIGINAL_IMPACT");
  // Rewards stay off until their own transaction exists.
  REQUIRE(life.at("lootEnabled").get<bool>() == false);
  REQUIRE(life.at("xpEnabled").get<bool>() == false);
  REQUIRE(life.at("respawnEnabled").get<bool>() == false);
  REQUIRE(life.at("rewards").at("loot").get<std::string>() == "PENDING");

  // Replay of the same operation acknowledges, never kills twice.
  const auto replay = SrpOriginalImpact::Apply(w, testUserId, opId);
  REQUIRE(replay.at("duplicate").get<bool>() == true);
  const auto after = SrpOriginalLifecycle::Inspect(cow);
  REQUIRE(after.at("lifecycle").at("death").at("operationId").get<std::string>() == opId);
  REQUIRE(after.at("lifecycle").at("epoch").get<int>() == 0);
  REQUIRE(cow.IsDead());

  // A second swing is refused while the killing swing is still pending in the
  // attacker outbox, even though the target is already dead.
  REQUIRE_THROWS_WITH(SrpOriginalImpact::Begin(w, testUserId, cowId, "req-lethal-2"),
                      "SRP_IMPACT_PENDING_SWING_EXISTS");

  // Reconcile against the durable corpse: the operation must close as APPLIED and
  // release the pending slot, not stay issued forever.
  WaitForSaved(w, cow);
  const auto reconciled = SrpOriginalImpact::Apply(w, testUserId, opId);
  REQUIRE(reconciled.at("state").get<std::string>() == "APPLIED");
  REQUIRE(reconciled.at("duplicate").get<bool>() == true);
  // Reconciling twice is still the same durable death.
  const auto reconciledAgain = SrpOriginalImpact::Apply(w, testUserId, opId);
  REQUIRE(reconciledAgain.at("state").get<std::string>() == "APPLIED");
  const auto closed = SrpOriginalLifecycle::Inspect(cow);
  REQUIRE(closed.at("lifecycle").at("death").at("operationId").get<std::string>() == opId);
  REQUIRE(closed.at("lifecycle").at("epoch").get<int>() == 0);

  // With the outbox closed the refusal is the target's own state: it is dead.
  REQUIRE_THROWS_WITH(SrpOriginalImpact::Begin(w, testUserId, cowId, "req-lethal-3"),
                      "SRP_IMPACT_ACTOR_INACTIVE");

  class LootOpenRecorder : public PartOneListener
  {
  public:
    std::vector<nlohmann::json> requests;
    void OnConnect(Networking::UserId) override {}
    void OnDisconnect(Networking::UserId) override {}
    void OnCustomPacket(Networking::UserId, const simdjson::dom::element&) override {}
    bool OnMpApiEvent(const GameModeEvent& event) override
    {
      if (std::string(event.GetName()) == "onSrpOriginalLootOpen")
        requests.push_back(nlohmann::json::parse(event.GetArgumentsJsonArray()));
      return true;
    }
  };
  const auto recorder = std::make_shared<LootOpenRecorder>();
  w.AddListener(recorder);
  const auto corpseInventory = cow.GetInventory().ToJson();
  const auto playerInventory = player.GetInventory().ToJson();
  cow.Activate(player);
  REQUIRE(recorder->requests == std::vector<nlohmann::json>{
    nlohmann::json::array({cowId, playerId})});
  cow.Activate(player, false, true);
  cow.Activate(player, true, false);
  REQUIRE(recorder->requests.size() == 1);
  REQUIRE(cow.GetInventory().ToJson() == corpseInventory);
  REQUIRE(player.GetInventory().ToJson() == playerInventory);
  REQUIRE_THROWS_WITH(cow.TakeItem(player, Inventory::Entry(15, 1)),
    "SRP_ORIGINAL_LOOT_LIFECYCLE_NOT_APPROVED");
}

TEST_CASE("Cancel completes a saved lethal request before acknowledging its receipt",
          "[srp-original-impact]")
{
  auto& server = GetPartOne();
  AttachStorage(server);
  auto& cow = SetupCow(server);
  auto& player = SetupPlayer(server, cow);
  Isolate(server, cow, player);
  auto& world = server.worldState;
  const auto descriptor = [&](uint32_t id) {
    return FormDesc::FromFormId(id, world.espmFiles).ToString();
  };
  const auto cell = cow.GetCellOrWorld().ToFormId(world.espmFiles);
  world.InstallSrpCollisionScene(CreateSyntheticScene(cell, descriptor(cow.GetBaseId()),
    descriptor(cow.GetRaceId()), cow.GetPos(), descriptor(player.GetBaseId()),
    descriptor(player.GetRaceId())), AuditConfig(cell, true));
  auto health = cow.GetActorValues();
  health.healthPercentage = .02f;
  cow.SetPercentages(health, nullptr);
  WaitForSaved(server, cow);
  const auto alive = cow.GetChangeForm();
  const auto issued = SrpOriginalImpact::Begin(server, testUserId, cowId, "cancel-durable-death");
  const auto operationId = issued.at("operationId").get<std::string>();
  WaitForSaved(server, player);
  const auto applied = SrpOriginalImpact::Apply(server, testUserId, operationId);
  REQUIRE(applied.at("lethal").get<bool>());
  const auto complete = cow.GetChangeForm().dynamicFields.GetAsJson();

  // Reconstruct the exact durable boundary before Kill's corpse transaction.
  // The hit receipt and DYING request exist, while health/drops remain original.
  auto interrupted = alive;
  auto fields = interrupted.dynamicFields.GetAsJson();
  fields[SrpOriginalImpact::kProperty] = complete.at(SrpOriginalImpact::kProperty);
  auto lifecycle = complete.at("private.srpOriginalLifecycleV1");
  auto pending = lifecycle.at("death");
  pending.erase("observedAtMs");
  pending["requestedAtMs"] = applied.at("targetReceipt").at("appliedAtMs");
  lifecycle["state"] = "DYING";
  lifecycle["pendingDeath"] = pending;
  lifecycle.erase("death");
  lifecycle.erase("rewards");
  lifecycle.erase("epochs");
  fields["private.srpOriginalLifecycleV1"] = lifecycle;
  interrupted.dynamicFields = DynamicFields::FromJson(fields);
  cow.ApplyChangeForm(interrupted);
  WaitForSaved(server, cow);
  REQUIRE_FALSE(cow.IsDead());
  REQUIRE(SrpOriginalLifecycle::AwaitingCorpse(cow));

  auto conflicting = interrupted;
  auto conflictingFields = fields;
  conflictingFields["private.srpOriginalLifecycleV1"]["pendingDeath"]["operationId"] = "foreign-death";
  conflicting.dynamicFields = DynamicFields::FromJson(conflictingFields);
  cow.ApplyChangeForm(conflicting);
  WaitForSaved(server, cow);
  REQUIRE_THROWS_WITH(SrpOriginalImpact::Cancel(server, testUserId, operationId),
                      "SRP_IMPACT_DEATH_RECEIPT_CONFLICT");
  REQUIRE_FALSE(cow.IsDead());
  REQUIRE(cow.GetInventory().ToJson() == alive.inv.ToJson());
  cow.ApplyChangeForm(interrupted);
  WaitForSaved(server, cow);

  const auto recovering = SrpOriginalImpact::Cancel(server, testUserId, operationId);
  REQUIRE(recovering.at("state").get<std::string>() == "TARGET_SAVE_PENDING");
  REQUIRE(cow.IsDead());
  REQUIRE_FALSE(SrpOriginalLifecycle::AwaitingCorpse(cow));
  WaitForSaved(server, cow);
  const auto acknowledged = SrpOriginalImpact::Cancel(server, testUserId, operationId);
  REQUIRE(acknowledged.at("state").get<std::string>() == "APPLIED");
  const auto inventory = cow.GetInventory().ToJson();
  const auto death = SrpOriginalLifecycle::Inspect(cow).at("lifecycle");
  REQUIRE(death.at("death").at("operationId") == operationId);
  SrpOriginalImpact::Cancel(server, testUserId, operationId);
  REQUIRE(cow.GetInventory().ToJson() == inventory);
  REQUIRE(SrpOriginalLifecycle::Inspect(cow).at("lifecycle") == death);
}
