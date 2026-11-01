#include "SrpOriginalImpact.h"
#include "SrpCollision.h"
#include "SrpActorStats.h"
#include "SrpOriginalReference.h"
#include "SrpOriginalStats.h"
#include "SrpOriginalLifecycle.h"
#include "MpActor.h"
#include "PartOne.h"
#include "DynamicFields.h"
#include "database_drivers/FileDatabase.h"
#include "save_storages/AsyncSaveStorage.h"
#include <catch2/catch_all.hpp>
#include <filesystem>
#include <unistd.h>

PartOne& GetPartOne();

namespace {
constexpr uint32_t cowId = 0x10ebaf;
constexpr uint32_t playerId = 0xff000091;
constexpr Networking::UserId testUserId = 7;

void AttachStorage(PartOne& w)
{
  const auto path = std::filesystem::temp_directory_path() /
    ("srp-impact-" + std::to_string(getpid()));
  using Save = Viet::AsyncSaveStorage<MpChangeForm, FormDesc, std::vector<FormDesc>>;
  w.worldState.AttachSaveStorage(std::make_shared<Save>(
    std::make_shared<FileDatabase>(path.string(), std::make_shared<spdlog::logger>("impact"), true)));
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
  const std::string& attackerRace)
{
  const std::string dummyHash(64, 'a');
  // The audited coverage box must actually contain the real fixture position.
  // A fixed world-space box would silently drop the coverage region (and thus
  // never prove the attack segment clear) whenever the fixture sits far from
  // the origin, so the box is anchored on the target instead.
  const double x = center.x, y = center.y, z = center.z;
  nlohmann::json shapes = nlohmann::json::array(
    {ActorShape(baseDesc, raceDesc), ActorShape(attackerBase, attackerRace)});
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
                                    playerBase, playerRace);
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
}