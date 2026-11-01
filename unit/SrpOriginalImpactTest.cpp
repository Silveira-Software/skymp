#include "SrpOriginalImpact.h"
#include "SrpCollision.h"
#include "SrpActorStats.h"
#include "SrpOriginalReference.h"
#include "SrpOriginalStats.h"
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

std::shared_ptr<const SrpCollision::Scene> CreateSyntheticScene(
  uint32_t cellId, const std::string& baseDesc, const std::string& raceDesc,
  const NiPoint3& center)
{
  const std::string dummyHash(64, 'a');
  // The audited coverage box must actually contain the real fixture position.
  // A fixed world-space box would silently drop the coverage region (and thus
  // never prove the attack segment clear) whenever the fixture sits far from
  // the origin, so the box is anchored on the target instead.
  const double x = center.x, y = center.y, z = center.z;
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
    {"actorShapes", nlohmann::json::array({{
      {"baseDesc", baseDesc},
      {"raceDesc", raceDesc},
      {"policy", "SERVER_MESH_PROXY_V1"},
      {"representation", "havok-triangles"},
      {"asset", "cow-proxy.nif"},
      {"localTransform", {1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, 0}},
      {"vertices", {
        {-40, -40, 0}, {40, -40, 0}, {40, 40, 128}, {-40, 40, 128}
      }},
      {"triangles", {{0, 1, 2}, {0, 2, 3}}}
    }})}
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

  auto scene = CreateSyntheticScene(cellId, baseDesc, raceDesc, cow.GetPos());
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
  auto scene = CreateSyntheticScene(cellId, "OtherBase:Skyrim.esm", "OtherRace:Skyrim.esm", cow.GetPos());
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
  auto scene = CreateSyntheticScene(cellId, baseDesc, raceDesc, cow.GetPos());
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
  auto scene = CreateSyntheticScene(cellId, baseDesc, raceDesc, cow.GetPos());

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
