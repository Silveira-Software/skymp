#include "SrpOriginalImpact.h"
#include "SrpSupernaturalCombat.h"
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
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sodium.h>

PartOne& GetPartOne();

TEST_CASE("R47 produces a native humanoid corpse fixture for the feeding consumer", "[.][srp-r47-feed-seed]")
{
  // Server fixture, not a client attack or an assertion of visual gameplay.
  // Exercise the existing death producer instead of inventing a lifecycle JSON.
  const char* directory = std::getenv("SRP_R47_SEED_DIR");
  REQUIRE(directory != nullptr);
  auto& w = GetPartOne();
  using Save = Viet::AsyncSaveStorage<MpChangeForm, FormDesc, std::vector<FormDesc>>;
  auto database = std::make_shared<FileDatabase>(directory, std::make_shared<spdlog::logger>("r47-seed"), true);
  w.worldState.AttachSaveStorage(std::make_shared<Save>(database));
  w.worldState.npcEnabled = true;
  auto& actor = w.worldState.GetFormAt<MpActor>(0x40413);
  w.worldState.npcEnabled = false;
  const auto save = [&] {
    const auto generation = w.worldState.GetChangeFormGeneration(actor.GetFormId());
    for (int i=0;i<2200 && !w.worldState.IsChangeFormSaved(actor.GetFormId(),generation);++i) {
      w.Tick(); std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    REQUIRE(w.worldState.IsChangeFormSaved(actor.GetFormId(),generation));
  };
  SrpOriginalReference::Adopt(actor, {{"speciesId","humanoid"},{"policyRevision","r47-feeding-fixture"}});
  save(); actor.ApplySrpOriginalStats(); save();
  REQUIRE(actor.GetRaceId() == 0x97a3d);
  actor.RunChangeFormTransaction([&] {
    SrpOriginalLifecycle::BeginAuthoritativeDeath(actor, actor, "r47-native-fixture-death", 1000);
    return true;
  });
  actor.Kill(); save();
  const auto proof = SrpOriginalLifecycle::Inspect(actor);
  REQUIRE(proof.at("state") == "DEAD");
  REQUIRE(proof.at("saved") == true);
  REQUIRE(proof.at("lifecycle").at("death").at("nativeDropsMaterialized") == true);
}

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

void WaitForSaved(PartOne& w, MpObjectReference& actor)
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

nlohmann::json SyntheticReferenceBinding(const MpObjectReference& ref, WorldState& world)
{
  const auto desc = [&](uint32_t id) { return FormDesc::FromFormId(id, world.espmFiles).ToString(); };
  const auto p = ref.GetPos();
  return {{"worldOrCell", ref.GetCellOrWorld().ToFormId(world.espmFiles)},
    {"reference", desc(ref.GetFormId())}, {"base", desc(ref.GetBaseId())},
    {"asset", "cow-proxy.nif"}, {"representation", "havok-triangles"},
    {"worldTransform", {1,0,0,p.x, 0,1,0,p.y, 0,0,1,p.z}},
    {"referenceTransform", {1,0,0,p.x, 0,1,0,p.y, 0,0,1,p.z}},
    {"vertices", {{0,0,0},{1,0,0},{0,1,0}}}, {"triangles", {{0,1,2}}}};
}

std::shared_ptr<const SrpCollision::Scene> CreateSyntheticScene(
  uint32_t cellId, const std::string& baseDesc, const std::string& raceDesc,
  const NiPoint3& center, const std::string& attackerBase,
  const std::string& attackerRace, bool controllerEnvelope = false,
  bool wall = false, double wallDistance = 25., double wallPadding = 0.,
  const nlohmann::json& extraGeometry = nlohmann::json::array())
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
    const auto wallY = y - wallDistance;
    doc["geometry"].push_back({
      {"worldOrCell", cellId}, {"reference", "1:fixture.esm"},
      {"base", "2:fixture.esm"}, {"asset", "cow-proxy.nif"},
      {"representation", "havok-triangles"},
      {"worldTransform", {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0}},
      {"referenceTransform", {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0}},
      {"vertices", {{x-50, wallY, z-wallPadding}, {x+50, wallY, z-wallPadding},
                    {x+50, wallY, z+128+wallPadding}, {x-50, wallY, z+128+wallPadding}}},
      {"triangles", {{0, 1, 2}, {0, 2, 3}}}
    });
  }
  for (const auto& row : extraGeometry) doc["geometry"].push_back(row);
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

// Private server fixtures. The integrated probe obtains these properties from
// the real canonical service and transformation coordinator instead.
nlohmann::json TestSeal(nlohmann::json value)
{
  value.erase("checksum");
  const auto text = value.dump();
  unsigned char bytes[crypto_hash_sha256_BYTES]; char out[65];
  crypto_hash_sha256(bytes, reinterpret_cast<const unsigned char*>(text.data()), text.size());
  sodium_bin2hex(out, sizeof(out), bytes, sizeof(bytes));
  value["checksum"] = out; return value;
}
void SetupClaws(PartOne& w, MpActor& player)
{
  Appearance appearance; appearance.raceId = 0xcdd84; player.SetAppearance(&appearance);
  const auto owner = nlohmann::json{{"profileId", "9191"}, {"characterId", "combat-PC"}};
  const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::system_clock::now().time_since_epoch()).count();
  player.SetPropertyValueDump("private.srpSupernaturalCombatV1", TestSeal({
    {"schemaVersion",1},{"owner",owner},{"revision",3},{"kind","werewolf"},{"form","werewolf"},
    {"phase","NONE"},{"reserve",100},{"expiresAtMs",now+90000},{"clawsEnabled",true}}).dump(),false,false);
  player.SetPropertyValueDump("private.srpTransformationV1", TestSeal({
    {"schemaVersion",1},{"owner",owner},{"activeOperation","fixture-transform"},{"pending",nullptr},
    {"fenced",false},{"restoration",{{"fixture",true}}},
    {"operations",{{"fixture-transform",{{"state","APPLIED"},{"target","werewolf"},
      {"targetAppearance",{{"raceId",0xcdd84}}}}}}}}).dump(),false,false);
  WaitForSaved(w,player);
}
}

TEST_CASE("R48 exports an isolated native combat world", "[.][srp-r48-seed]")
{
  const char* directory = std::getenv("SRP_R48_SEED_DIR"); REQUIRE(directory);
  auto& w = GetPartOne(); AttachStorage(w, std::filesystem::path(directory)/"world");
  auto& cow = SetupCow(w); auto& player = SetupPlayer(w,cow);
  const auto desc = [&](uint32_t id) { return FormDesc::FromFormId(id,w.worldState.espmFiles).ToString(); };
  nlohmann::json facts={{"actorId",playerId},{"targetId",cowId},{"pos",{cow.GetPos().x,cow.GetPos().y,cow.GetPos().z}},
    {"worldOrCell",cow.GetCellOrWorld().ToFormId(w.worldState.espmFiles)},
    {"targetBase",desc(cow.GetBaseId())},{"targetRace",desc(cow.GetRaceId())},
    {"playerBase",desc(player.GetBaseId())},{"playerRace",desc(player.GetRaceId())}};
  std::ofstream(std::filesystem::path(directory)/"facts.json") << facts.dump(2);
}

TEST_CASE("Claws require the canonical condition, reserve, transformation and contact", "[srp-r48-claws]")
{
  auto& w=GetPartOne(); AttachStorage(w);
  auto& cow=SetupCow(w); auto& player=SetupPlayer(w,cow); Isolate(w,cow,player);
  REQUIRE_THROWS_WITH(SrpOriginalImpact::Begin(w,testUserId,cowId,"claw-human"),"SRP_CLAWS_WEREWOLF_RACE_REQUIRED");
  SetupClaws(w,player);
  const auto good=player.GetChangeForm();
  auto state=player.GetDynamicFields().GetAsJson().at("private.srpSupernaturalCombatV1");
  for (const auto& change : std::vector<nlohmann::json>{{{"reserve",0}},{{"expiresAtMs",0}},{{"form","human"}},{{"clawsEnabled",false}}}) {
    auto denied=state;denied.update(change);
    player.SetPropertyValueDump("private.srpSupernaturalCombatV1",TestSeal(denied).dump(),false,false);WaitForSaved(w,player);
    REQUIRE_THROWS_WITH(SrpOriginalImpact::Begin(w,testUserId,cowId,"claw-denied"),"SRP_CLAWS_CONDITION_UNAVAILABLE");
  }
  player.ApplyChangeForm(good);WaitForSaved(w,player);
  const auto desc=[&](uint32_t id){return FormDesc::FromFormId(id,w.worldState.espmFiles).ToString();};
  const auto cell=cow.GetCellOrWorld().ToFormId(w.worldState.espmFiles);
  w.worldState.InstallSrpCollisionScene(CreateSyntheticScene(cell,desc(cow.GetBaseId()),desc(cow.GetRaceId()),cow.GetPos(),
    desc(player.GetBaseId()),desc(player.GetRaceId()),false,true),AuditConfig(cell,true));
  REQUIRE_THROWS(SrpOriginalImpact::Begin(w,testUserId,cowId,"claw-wall"));
  REQUIRE_FALSE(SrpOriginalImpact::Inspect(player).at("present"));
  w.worldState.InstallSrpCollisionScene(CreateSyntheticScene(cell,desc(cow.GetBaseId()),desc(cow.GetRaceId()),cow.GetPos(),
    desc(player.GetBaseId()),desc(player.GetRaceId())),AuditConfig(cell,true));
  auto far=player.GetPos();far.y-=500;player.SetPos(far);WaitForSaved(w,player);
  REQUIRE_THROWS(SrpOriginalImpact::Begin(w,testUserId,cowId,"claw-far"));
  player.ApplyChangeForm(good);WaitForSaved(w,player);
  const auto op=SrpOriginalImpact::Begin(w,testUserId,cowId,"claw-return").at("operationId").get<std::string>();
  WaitForSaved(w,player);
  Appearance human;human.raceId=0x13746;player.SetAppearance(&human);WaitForSaved(w,player);
  const auto before=cow.GetActorValues().healthPercentage;
  REQUIRE_THROWS_WITH(SrpOriginalImpact::Apply(w,testUserId,op),"SRP_IMPACT_WEAPON_CHANGED");
  REQUIRE(SrpOriginalImpact::Cancel(w,testUserId,op).at("state")=="CANCELLED");
  REQUIRE(cow.GetActorValues().healthPercentage==before);
}

TEST_CASE("Werewolf claw contact survives valid movement before the durable apply",
          "[srp-r49-claw-contact]")
{
  const auto scenario = GENERATE(0, 1, 2, 3);
  CAPTURE(scenario);
  const bool baseline = std::getenv("SRP_R49_EXPECT_EXACT_CLAW_FRAME") != nullptr;
  auto& server = GetPartOne();
  AttachStorage(server);
  auto& target = SetupCow(server);
  auto& player = SetupPlayer(server, target);
  Isolate(server, target, player);
  SetupClaws(server, player);
  server.SetDamageFormula(std::make_unique<TES5DamageFormula>());

  auto& world = server.worldState;
  const auto cell = target.GetCellOrWorld().ToFormId(world.espmFiles);
  const auto descriptor = [&](uint32_t id) {
    return FormDesc::FromFormId(id, world.espmFiles).ToString();
  };
  const auto install = [&](bool changed) {
    // A wall beyond the target changes the scene identity without obscuring
    // this contact. An issued action cannot migrate to a different scene.
    world.InstallSrpCollisionScene(CreateSyntheticScene(cell, descriptor(target.GetBaseId()),
      descriptor(target.GetRaceId()), target.GetPos(), descriptor(player.GetBaseId()),
      descriptor(player.GetRaceId()), false, changed, -130.), AuditConfig(cell, true));
  };
  install(false);
  const auto healthBefore = target.GetActorValues().healthPercentage;
  const auto issued = SrpOriginalImpact::Begin(server, testUserId, cowId, "claw-movement-before-apply");
  const auto operation = issued.at("operationId").get<std::string>();
  WaitForSaved(server, player);
  const auto admitted = SrpOriginalImpact::Inspect(player).at("ledger").at("operations")
    .at(operation).at("intent").at("proof");
  REQUIRE(admitted.at("collision") == "CLEAR");

  if (scenario == 1) {
    player.SetAngle({0, 0, 180});
    WaitForSaved(server, player);
    REQUIRE_THROWS_WITH(SrpOriginalImpact::Apply(server, testUserId, operation),
                        "SRP_IMPACT_SERVER_SEGMENT_MISSES_TARGET");
  } else if (scenario == 2) {
    target.SetPropertyValueDump("r49-claw-contact-fixture", "1", false, false);
    WaitForSaved(server, target);
    REQUIRE_THROWS_WITH(SrpOriginalImpact::Apply(server, testUserId, operation),
                        "SRP_IMPACT_GENERATION_CHANGED");
  } else if (scenario == 3) {
    install(true);
    REQUIRE_THROWS_WITH(SrpOriginalImpact::Apply(server, testUserId, operation),
                        "SRP_IMPACT_FRAME_CHANGED");
  } else {
    auto position = player.GetPos();
    position.y += 2;
    player.SetPos(position);
    player.SetAngle({9, 0, 2});
    WaitForSaved(server, player);
    if (!baseline) {
      const auto result = SrpOriginalImpact::Apply(server, testUserId, operation);
      REQUIRE(result.at("state") == "TARGET_SAVE_PENDING");
      const auto& receipt = result.at("targetReceipt");
      REQUIRE(receipt.at("intent").at("proof") == admitted);
      REQUIRE(receipt.at("proof") != admitted);
      REQUIRE(receipt.at("proof").at("collision") == "CLEAR");
      REQUIRE(receipt.at("damage").get<float>() > 0.f);
      const auto healthAfter = target.GetActorValues().healthPercentage;
      REQUIRE(healthAfter < healthBefore);
      WaitForSaved(server, target);
      REQUIRE(SrpOriginalImpact::Apply(server, testUserId, operation).at("state") == "APPLIED");
      WaitForSaved(server, player);
      REQUIRE(SrpOriginalImpact::Apply(server, testUserId, operation).at("duplicate") == true);
      REQUIRE(target.GetActorValues().healthPercentage == healthAfter);
      return;
    }
    REQUIRE_THROWS_WITH(SrpOriginalImpact::Apply(server, testUserId, operation),
                        "SRP_IMPACT_FRAME_CHANGED");
  }
  REQUIRE(SrpOriginalImpact::Cancel(server, testUserId, operation).at("state") == "CANCELLED");
  REQUIRE(target.GetActorValues().healthPercentage == healthBefore);
}

TEST_CASE("Vampire drain escrows one cost, applies resistance and heals once", "[srp-r48-drain]")
{
  const int resistance=GENERATE(0,30,103);
  const bool lord=GENERATE(false,true);
  auto& w=GetPartOne();AttachStorage(w);auto& target=SetupCow(w);auto& player=SetupPlayer(w,target);Isolate(w,target,player);
  const auto source=uint32_t(lord?0x02019324:0x8d5bf);
  REQUIRE_THROWS_WITH(SrpOriginalImpact::Begin(w,testUserId,cowId,"drain-human",source),"SRP_DRAIN_CONDITION_REQUIRED");
  player.AddSpell(source);Equipment equipment;equipment.leftSpell=source;player.SetEquipment(equipment);
  if(lord){
    SetupClaws(w,player);Appearance appearance;appearance.raceId=0x0200283a;player.SetAppearance(&appearance);
    auto ledger=player.GetDynamicFields().GetAsJson().at("private.srpTransformationV1");
    auto& entry=ledger["operations"]["fixture-transform"];
    entry["target"]="vampireLord";entry["targetAppearance"]["raceId"]=appearance.raceId;
    player.SetPropertyValueDump("private.srpTransformationV1",TestSeal(ledger).dump(),false,false);
  }
  const auto now=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
  player.SetPropertyValueDump("private.srpSupernaturalCombatV1",TestSeal({{"schemaVersion",1},
    {"owner",{{"profileId","9191"},{"characterId","combat-PC"}}},{"kind","vampire"},{"form",lord?"vampireLord":"human"},
    {"phase","NONE"},{"reserve",100},{"expiresAtMs",now+90000},{"drainEnabled",true},{"drainSpell",source}}).dump(),false,false);
  player.DamageActorValue(espm::ActorValue::Health,20);
  if(resistance==30)target.AddSpell(0x53127);
  if(resistance==103){target.AddSpell(0x53127);target.AddSpell(0xaa01f);target.AddSpell(0xcc79a);target.AddSpell(0xf2595);}
  WaitForSaved(w,player);WaitForSaved(w,target);
  const auto desc=[&](uint32_t id){return FormDesc::FromFormId(id,w.worldState.espmFiles).ToString();};
  const auto cell=target.GetCellOrWorld().ToFormId(w.worldState.espmFiles);
  w.worldState.InstallSrpCollisionScene(CreateSyntheticScene(cell,desc(target.GetBaseId()),desc(target.GetRaceId()),target.GetPos(),
    desc(player.GetBaseId()),desc(player.GetRaceId())),AuditConfig(cell,true));
  const auto mana=[&]{return player.GetActorValues().magickaPercentage*player.GetMaximumValues().magicka;};
  const auto health=[&]{return player.GetActorValues().healthPercentage*player.GetMaximumValues().health;};
  const auto beforeMana=mana(),beforeHealth=health(),beforeTarget=target.GetActorValues().healthPercentage;
  const auto op=SrpOriginalImpact::Begin(w,testUserId,cowId,"hit-drain-test",source).at("operationId").get<std::string>();
  const auto weapon=SrpOriginalImpact::Inspect(player).at("ledger").at("operations").at(op).at("intent").at("weapon");
  REQUIRE(weapon.at("policy")=="AIMED_DRAIN_SPHERE_V3");
  REQUIRE(weapon.at("projectile").get<uint32_t>()>0);
  REQUIRE(weapon.at("collisionRadius").get<double>()>=0);
  if(lord)REQUIRE(weapon.at("collisionRadius")==24);
  const float cost=lord?10:6;
  WaitForSaved(w,player);REQUIRE(mana()==Catch::Approx(beforeMana-cost));
  REQUIRE_THROWS_WITH(SrpOriginalImpact::Apply(w,testUserId,op),"SRP_DRAIN_CHANNEL_PENDING");
  std::this_thread::sleep_for(std::chrono::milliseconds(1100));
  REQUIRE_THROWS_WITH(SrpOriginalImpact::Apply(w,testUserId,op),"SRP_DRAIN_CHANNEL_PENDING");
  const auto applied=SrpOriginalImpact::Apply(w,testUserId,op,true);REQUIRE(applied.at("state")=="TARGET_SAVE_PENDING");
  REQUIRE(applied.at("targetReceipt").at("proof").at("projectileRadius")==weapon.at("collisionRadius"));
  const float expected=(lord?15.f:2.f)*(1.f-std::min(resistance,100)/100.f);
  REQUIRE(applied.at("targetReceipt").at("damage").get<float>()==Catch::Approx(expected));
  REQUIRE(health()==Catch::Approx(beforeHealth));
  WaitForSaved(w,target);REQUIRE(SrpOriginalImpact::Apply(w,testUserId,op).at("state")=="APPLIED");WaitForSaved(w,player);
  REQUIRE(health()==Catch::Approx(beforeHealth+expected));REQUIRE(mana()==Catch::Approx(beforeMana-cost));
  const auto targetAfter=target.GetActorValues().healthPercentage;
  REQUIRE(SrpOriginalImpact::Begin(w,testUserId,cowId,"hit-drain-test",source).at("duplicate")==true);
  SrpOriginalImpact::Apply(w,testUserId,op);SrpOriginalImpact::Cancel(w,testUserId,op);
  REQUIRE(health()==Catch::Approx(beforeHealth+expected));REQUIRE(mana()==Catch::Approx(beforeMana-cost));
  REQUIRE(target.GetActorValues().healthPercentage==targetAfter);
  if(resistance==103)REQUIRE(targetAfter==beforeTarget);
  const auto cancelled=SrpOriginalImpact::Begin(w,testUserId,cowId,"hit-drain-cancel",source).at("operationId").get<std::string>();
  WaitForSaved(w,player);REQUIRE(mana()==Catch::Approx(beforeMana-cost*2));
  SrpOriginalImpact::Cancel(w,testUserId,cancelled);WaitForSaved(w,player);
  REQUIRE(mana()==Catch::Approx(beforeMana-cost));SrpOriginalImpact::Cancel(w,testUserId,cancelled);
  REQUIRE(mana()==Catch::Approx(beforeMana-cost));REQUIRE(target.GetActorValues().healthPercentage==targetAfter);
  auto empty=player.GetActorValues();empty.magickaPercentage=0;player.SetPercentages(empty,nullptr);WaitForSaved(w,player);
  REQUIRE_THROWS_WITH(SrpOriginalImpact::Begin(w,testUserId,cowId,"drain-no-mana",source),"SRP_DRAIN_INSUFFICIENT_MAGICKA");
}

TEST_CASE("A live drain retries its same action after a prior save without charging twice", "[srp-r49-drain-save-ack]")
{
  using Json=nlohmann::json;
  const bool dirtyTarget=GENERATE(false,true);
  const bool legacy=std::getenv("SRP_R49_EXPECT_TERMINAL_SAVE_ACK")!=nullptr;
  auto& server=GetPartOne();AttachStorage(server);
  auto& target=SetupCow(server);auto& player=SetupPlayer(server,target);
  Isolate(server,target,player);
  const uint32_t source=0x8d5bf;
  player.AddSpell(source);Equipment equipment;equipment.rightSpell=source;player.SetEquipment(equipment);
  const auto now=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
  player.SetPropertyValueDump("private.srpSupernaturalCombatV1",TestSeal({{"schemaVersion",1},
    {"owner",{{"profileId","9191"},{"characterId","combat-PC"}}},{"kind","vampire"},{"form","human"},
    {"phase","NONE"},{"reserve",100},{"expiresAtMs",now+90000},{"drainEnabled",true},{"drainSpell",source}}).dump(),false,false);
  WaitForSaved(server,player);WaitForSaved(server,target);
  auto& world=server.worldState;
  const auto desc=[&](uint32_t id){return FormDesc::FromFormId(id,world.espmFiles).ToString();};
  const auto cell=target.GetCellOrWorld().ToFormId(world.espmFiles);
  world.InstallSrpCollisionScene(CreateSyntheticScene(cell,desc(target.GetBaseId()),desc(target.GetRaceId()),
    target.GetPos(),desc(player.GetBaseId()),desc(player.GetRaceId())),AuditConfig(cell,true));
  struct Listener : PartOne::Listener {
    PartOne* server=nullptr;unsigned calls=0;Json result;std::string error;
    void OnConnect(Networking::UserId) override {}
    void OnDisconnect(Networking::UserId) override {}
    void OnCustomPacket(Networking::UserId,const simdjson::dom::element&) override {}
    bool OnMpApiEvent(const GameModeEvent& event) override {
      if(!server||std::string(event.GetName())!="onSrpOriginalImpactRequest")return true;
      ++calls;const auto args=Json::parse(event.GetArgumentsJsonArray());
      try{result=SrpOriginalImpact::Begin(*server,args.at(0).get<Networking::UserId>(),
        args.at(1).get<uint32_t>(),args.at(2).get<std::string>(),args.at(3).get<uint32_t>());}
      catch(const std::exception& e){error=e.what();}
      return false;
    }
  };
  auto listener=std::make_shared<Listener>();server.AddListener(listener);listener->server=&server;
  const auto send=[&](const Json& value){
    server.Messages().clear();RawMessageData raw;raw.userId=testUserId;
    CustomPacketMessage packet;packet.contentJsonDump=value.dump();
    server.GetActionListener().OnCustomPacket(raw,packet);
  };
  send({{"customPacketType","srpOriginalMelee"},{"version",1},{"action","hello"}});
  REQUIRE(server.Messages().size()==1);
  const auto ready=Json::parse(server.Messages().back().j.at("contentJsonDump").get<std::string>());
  Json packet={{"customPacketType","srpOriginalMelee"},{"version",1},{"action","drain"},
    {"session",ready.at("session")},{"actionId",std::string(32,'d')},{"targetId",cowId},{"spellId",source}};
  const auto ack=[&]{
    std::vector<Json> replies;
    for(const auto& message:server.Messages())if(message.j.contains("contentJsonDump")){
      const auto reply=Json::parse(message.j.at("contentJsonDump").get<std::string>());
      if(reply.value("action",std::string())=="drainAck")replies.push_back(reply);
    }
    // A successful Begin also publishes its native Magicka escrow update.
    REQUIRE(replies.size()==1);
    const auto& reply=replies.front();
    REQUIRE(reply.at("actionId")==packet.at("actionId"));REQUIRE(reply.at("actorId")==playerId);
    return reply.at("state").get<std::string>();
  };
  const auto mana=[&]{return player.GetActorValues().magickaPercentage*player.GetMaximumValues().magicka;};
  const auto beforeMana=mana(),beforeHealth=target.GetActorValues().healthPercentage;
  auto& dirty=dirtyTarget?target:player;
  dirty.SetPropertyValueDump("r49-save-ack-fixture","1",false,false);
  const auto generation=world.GetChangeFormGeneration(dirty.GetFormId());
  REQUIRE_FALSE(world.IsChangeFormSaved(dirty.GetFormId(),generation));
  auto stale=packet;stale["session"]=std::string(64,'0');send(stale);
  REQUIRE(server.Messages().empty());REQUIRE(listener->calls==0);
  send(packet);
  REQUIRE(ack()==(legacy?"REFUSED":"PENDING"));
  REQUIRE(listener->calls==(legacy?1:0));
  if(legacy)REQUIRE(listener->error=="SRP_IMPACT_PRIOR_SAVE_PENDING");
  REQUIRE_FALSE(SrpOriginalImpact::Inspect(player).at("present"));
  REQUIRE(mana()==beforeMana);REQUIRE(target.GetActorValues().healthPercentage==beforeHealth);
  WaitForSaved(server,dirty);send(packet);
  REQUIRE(ack()=="ISSUED");
  REQUIRE(listener->result.at("state")=="ISSUED");
  const auto operation=listener->result.at("operationId").get<std::string>();
  REQUIRE(mana()==Catch::Approx(beforeMana-6));
  WaitForSaved(server,player);send(packet);
  REQUIRE(ack()=="ISSUED");
  REQUIRE(listener->result.at("duplicate")==true);
  REQUIRE(listener->result.at("operationId")==operation);
  REQUIRE(mana()==Catch::Approx(beforeMana-6));
  REQUIRE(target.GetActorValues().healthPercentage==beforeHealth);
  // Releasing the button still cancels/refunds the same durable operation.
  packet["action"]="interruptDrain";send(packet);WaitForSaved(server,player);
  REQUIRE(SrpOriginalImpact::Inspect(player).at("ledger").at("operations").at(operation).at("state")=="CANCELLED");
  REQUIRE(mana()==Catch::Approx(beforeMana));REQUIRE(target.GetActorValues().healthPercentage==beforeHealth);
  listener->server=nullptr;
}

TEST_CASE("Aimed drains follow accepted pitch and retain their physical guards", "[srp-r49-drain-pitch]")
{
  const int height = GENERATE(-160, 160);
  const bool lord = GENERATE(false, true);
  const int scenario = GENERATE(0, 1, 2, 3, 4);
  const bool legacy = std::getenv("SRP_R49_EXPECT_HORIZONTAL_REFUSAL") != nullptr;
  if (legacy && scenario != 0) return;
  auto& server = GetPartOne(); AttachStorage(server);
  auto& target = SetupCow(server); auto& player = SetupPlayer(server, target);
  Isolate(server, target, player);
  const uint32_t source = lord ? 0x02019324 : 0x8d5bf;
  player.AddSpell(source);
  Equipment equipment; equipment.leftSpell = source; player.SetEquipment(equipment);
  if (lord) {
    SetupClaws(server, player);
    Appearance appearance; appearance.raceId = 0x0200283a; player.SetAppearance(&appearance);
    auto ledger = player.GetDynamicFields().GetAsJson().at("private.srpTransformationV1");
    auto& entry = ledger["operations"]["fixture-transform"];
    entry["target"] = "vampireLord"; entry["targetAppearance"]["raceId"] = appearance.raceId;
    player.SetPropertyValueDump("private.srpTransformationV1", TestSeal(ledger).dump(), false, false);
  }
  const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::system_clock::now().time_since_epoch()).count();
  player.SetPropertyValueDump("private.srpSupernaturalCombatV1", TestSeal({{"schemaVersion",1},
    {"owner",{{"profileId","9191"},{"characterId","combat-PC"}}},{"kind","vampire"},
    {"form",lord?"vampireLord":"human"},{"phase","NONE"},{"reserve",100},
    {"expiresAtMs",now+90000},{"drainEnabled",true},{"drainSpell",source}}).dump(), false, false);
  auto position = target.GetPos();
  player.SetPos({position.x, position.y - 200.f, position.z});
  position.z += height; target.SetPos(position); target.SetAngle({0,0,0});
  const double pitch = std::atan2(70.4 - (height + 64.), 200.) * 180. / std::acos(-1.);
  CAPTURE(height, lord, scenario, pitch);
  player.SetAngle({static_cast<float>(scenario == 1 ? -pitch : scenario == 3 ? 120. : pitch),
    0, scenario == 4 ? 180.f : 0.f});
  player.DamageActorValue(espm::ActorValue::Health, 20);
  WaitForSaved(server, player); WaitForSaved(server, target);
  const auto descriptor = [&](uint32_t id) { return FormDesc::FromFormId(id, server.worldState.espmFiles).ToString(); };
  const auto cell = target.GetCellOrWorld().ToFormId(server.worldState.espmFiles);
  server.worldState.InstallSrpCollisionScene(CreateSyntheticScene(cell, descriptor(target.GetBaseId()),
    descriptor(target.GetRaceId()), target.GetPos(), descriptor(player.GetBaseId()), descriptor(player.GetRaceId()),
    // Keep the barrier ahead of the entire target. A wall inside its slanted
    // proxy can legitimately lie after the wide Lord projectile first touches.
    true, scenario == 2, 100., 256.), AuditConfig(cell, true));
  const auto before = player.GetActorValues();
  if (legacy || scenario != 0) {
    const std::string expected = legacy || scenario == 1 || scenario == 4 ? "SRP_IMPACT_SERVER_SEGMENT_MISSES_TARGET" :
      scenario == 2 ? "SRP_IMPACT_SERVER_COLLISION_NOT_CLEAR" : "SRP_DRAIN_AIM_PITCH_UNSUPPORTED";
    REQUIRE_THROWS_WITH(SrpOriginalImpact::Begin(server, testUserId, cowId, "inclined-drain", source), expected);
    REQUIRE(player.GetActorValues().magickaPercentage == before.magickaPercentage);
    REQUIRE_FALSE(SrpOriginalImpact::Inspect(player).at("present").get<bool>());
    return;
  }
  const auto operation = SrpOriginalImpact::Begin(server, testUserId, cowId, "inclined-drain", source).at("operationId").get<std::string>();
  WaitForSaved(server, player);
  std::this_thread::sleep_for(std::chrono::milliseconds(1100));
  const auto applied = SrpOriginalImpact::Apply(server, testUserId, operation, true);
  REQUIRE(applied.at("state") == "TARGET_SAVE_PENDING");
  const auto& receipt = applied.at("targetReceipt");
  REQUIRE(receipt.at("damage").get<float>() > 0);
  REQUIRE((receipt.at("proof").at("aimDirection").at(2).get<double>() > 0) == (height > 0));
  WaitForSaved(server, target);
  REQUIRE(SrpOriginalImpact::Apply(server, testUserId, operation).at("state") == "APPLIED");
  WaitForSaved(server, player);
  const auto after = player.GetActorValues();
  SrpOriginalImpact::Apply(server, testUserId, operation);
  REQUIRE(player.GetActorValues().healthPercentage == after.healthPercentage);
  REQUIRE(player.GetActorValues().magickaPercentage == after.magickaPercentage);
}

TEST_CASE("Aimed drain validates current contact after its admission frame moves", "[srp-r49-moving-drain]")
{
  const int change = GENERATE(0, 1, 2, 3, 4, 5, 6, 7);
  auto& server = GetPartOne(); AttachStorage(server);
  auto& target = SetupCow(server); auto& player = SetupPlayer(server, target);
  Isolate(server, target, player);
  const uint32_t source = 0x8d5bf;
  player.AddSpell(source);
  Equipment equipment; equipment.leftSpell = source; player.SetEquipment(equipment);
  const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::system_clock::now().time_since_epoch()).count();
  player.SetPropertyValueDump("private.srpSupernaturalCombatV1", TestSeal({{"schemaVersion",1},
    {"owner",{{"profileId","9191"},{"characterId","combat-PC"}}},{"kind","vampire"},{"form","human"},
    {"phase","NONE"},{"reserve",100},{"expiresAtMs",now+90000},{"drainEnabled",true},{"drainSpell",source}}).dump(),false,false);
  player.DamageActorValue(espm::ActorValue::Health, 20);
  WaitForSaved(server, player); WaitForSaved(server, target);
  const auto descriptor = [&](uint32_t id) { return FormDesc::FromFormId(id, server.worldState.espmFiles).ToString(); };
  const auto cell = target.GetCellOrWorld().ToFormId(server.worldState.espmFiles);
  const auto originalPosition = target.GetPos();
  const auto scene = [&](bool wall) { return CreateSyntheticScene(cell, descriptor(target.GetBaseId()),
    descriptor(target.GetRaceId()), originalPosition, descriptor(player.GetBaseId()), descriptor(player.GetRaceId()), true, wall); };
  server.worldState.InstallSrpCollisionScene(scene(false), AuditConfig(cell, true));
  const auto mana = [&] { return player.GetActorValues().magickaPercentage * player.GetMaximumValues().magicka; };
  const auto health = [&] { return player.GetActorValues().healthPercentage * player.GetMaximumValues().health; };
  const float beforeMana = mana(), beforeHealth = health(), targetHealth = target.GetActorValues().healthPercentage;
  const auto operation = SrpOriginalImpact::Begin(server, testUserId, cowId, "moving-drain", source).at("operationId").get<std::string>();
  WaitForSaved(server, player);
  std::this_thread::sleep_for(std::chrono::milliseconds(1100));
  if (change == 0 || change == 2 || change == 5 || change == 6) {
    auto position = target.GetPos(); position.x += change == 2 ? 500.f : 5.f;
    target.SetPos(position, SetPosMode::CalledByUpdateMovement);
  }
  if (change == 1) {
    auto position = player.GetPos(); position.x += 5.f;
    player.SetPos(position, SetPosMode::CalledByUpdateMovement);
  }
  if (change == 3) server.worldState.InstallSrpCollisionScene(scene(true), AuditConfig(cell, true));
  if (change == 4) server.worldState.InstallSrpCollisionScene(scene(false), AuditConfig(cell, false));
  if (change == 5) {
    target.SetPropertyValueDump("private.r49SaveChange", "true", false, false);
    WaitForSaved(server, target);
  }
  if (change == 6) {
    auto identity = target.GetDynamicFields().GetAsJson().at("private.srpOriginalReferenceV1");
    identity["instanceId"] = "r49-different-original-instance";
    target.SetPropertyValueDump("private.srpOriginalReferenceV1", identity.dump(), false, false);
    WaitForSaved(server, target);
  }
  if (change == 7) {
    auto position = originalPosition; position.y -= 25.f;
    server.CreateActor(0xff000092, position, 0, cell, 9192);
  }
  if (std::getenv("SRP_R49_EXPECT_LEGACY_FRAME_REJECTION")) {
    if (change < 2)
      REQUIRE_THROWS_WITH(SrpOriginalImpact::Apply(server, testUserId, operation, true), "SRP_IMPACT_FRAME_CHANGED");
    return;
  }
  if (change >= 2) {
    const std::string expected = change == 2 ? "SRP_IMPACT_SERVER_SEGMENT_MISSES_TARGET" :
      change == 3 ? "SRP_DRAIN_SCENE_CHANGED" :
      change == 4 ? "SRP_IMPACT_SERVER_FRAME_UNAVAILABLE" :
      change == 5 ? "SRP_IMPACT_GENERATION_CHANGED" :
      change == 6 ? "SRP_IMPACT_TARGET_IDENTITY_MISMATCH" : "SRP_IMPACT_SERVER_COLLISION_NOT_CLEAR";
    REQUIRE_THROWS_WITH(SrpOriginalImpact::Apply(server, testUserId, operation, true), expected);
    REQUIRE(target.GetActorValues().healthPercentage == targetHealth);
    REQUIRE(health() == Catch::Approx(beforeHealth));
    return;
  }
  const auto applied = SrpOriginalImpact::Apply(server, testUserId, operation, true);
  REQUIRE(applied.at("state") == "TARGET_SAVE_PENDING");
  const auto receipt = applied.at("targetReceipt");
  REQUIRE(receipt.at("proof") != receipt.at("intent").at("proof"));
  REQUIRE(receipt.at("proof").at("collision") == "CLEAR");
  REQUIRE(receipt.at("damage").get<float>() == Catch::Approx(2.f));
  WaitForSaved(server, target);
  REQUIRE(SrpOriginalImpact::Apply(server, testUserId, operation, true).at("state") == "APPLIED");
  WaitForSaved(server, player);
  REQUIRE(mana() == Catch::Approx(beforeMana - 6.f));
  REQUIRE(health() == Catch::Approx(beforeHealth + 2.f));
  const auto after = target.GetActorValues().healthPercentage;
  SrpOriginalImpact::Apply(server, testUserId, operation, true);
  REQUIRE(target.GetActorValues().healthPercentage == after);
  REQUIRE(health() == Catch::Approx(beforeHealth + 2.f));
}

TEST_CASE("Actor collision yaw rotates local forward in Skyrim's clockwise convention", "[srp-r49-actor-yaw]")
{
  auto& w=GetPartOne();AttachStorage(w);
  auto& cow=SetupCow(w);
  auto& player=SetupPlayer(w,cow);
  const auto before=player.GetChangeForm();
  const bool legacy=std::getenv("SRP_R49_EXPECT_COUNTERCLOCKWISE_POSE")!=nullptr;
  struct Facing { float degrees; double x,y; };
  // Golden forward vectors from NiMatrix3::SetEulerAnglesXYZ, not a copy of
  // CaptureSrpActorCollisionPose. In particular +90 faces +X, and -90 -X.
  for(const auto facing:std::vector<Facing>{{0,0,1},{90,1,0},{180,0,-1},
      {-90,-1,0},{-45,-.70710678118655,.70710678118655},
      {148.3330078125f,.52498141586914,-.85111368981590}}){
    CAPTURE(facing.degrees,legacy);
    player.SetPos({10,20,-3});player.SetAngle({0,0,facing.degrees});
    const auto pose=w.worldState.CaptureSrpActorCollisionPose(player);
    REQUIRE(pose.has_value());
    const auto point=[&](double x,double y,double z){return SrpCollision::Vec3{
      pose->at(0)*x+pose->at(1)*y+pose->at(2)*z+pose->at(3),
      pose->at(4)*x+pose->at(5)*y+pose->at(6)*z+pose->at(7),
      pose->at(8)*x+pose->at(9)*y+pose->at(10)*z+pose->at(11)};};
    const auto forward=point(0,1,0),right=point(1,0,0),up=point(0,0,1);
    REQUIRE(forward.x==Catch::Approx(10+(legacy?-facing.x:facing.x)).margin(1e-6));
    REQUIRE(forward.y==Catch::Approx(20+facing.y).margin(1e-6));
    REQUIRE(right.x==Catch::Approx(10+facing.y).margin(1e-6));
    REQUIRE(right.y==Catch::Approx(20+(legacy?facing.x:-facing.x)).margin(1e-6));
    REQUIRE(up.x==10);REQUIRE(up.y==20);REQUIRE(up.z==-2);
  }
  player.ApplyChangeForm(before);
}

TEST_CASE("Recorded first-person R49 drain contacts the correctly rotated original controller", "[srp-r49-actor-yaw]")
{
  const auto path=std::getenv("SRP_R49_RADIUS_SCENE");
  if(!path){SKIP("Pinned original R49 scene supplied by isolated build runner");}
  std::ifstream stream(path);nlohmann::json doc;stream>>doc;
  REQUIRE(SrpCollision::Scene::Digest(doc)=="b8eb35fbf317d758b12b60e1ac22b2db61d81ca8a78dfe8c10e787289f66b6b4");
  const auto scene=SrpCollision::Scene::Load(doc,doc.at("bindings"),SrpCollision::Scene::Digest(doc));
  auto& w=GetPartOne();AttachStorage(w);
  auto& cow=SetupCow(w);auto& actor=SetupPlayer(w,cow);
  const auto before=actor.GetChangeForm();
  // Exact accepted native frame, 15 November 2026 21:27:32.253Z. This is an
  // offline replay: no runtime actor or saved human position is overwritten.
  actor.SetPos({917.671875f,958.2507934570313f,-187.61988830566406f});
  actor.SetAngle({38.45392990112305f,0,148.3330078125f});
  const auto pose=w.worldState.CaptureSrpActorCollisionPose(actor);
  REQUIRE(pose.has_value());
  const auto target=scene->ActorTriangles("71aa:Dawnguard.esm","19fd3:Dawnguard.esm",*pose,false);
  REQUIRE(target.has_value());
  const SrpCollision::Vec3 from{981.1483154296875,853.9141235351563,-100.3135988657807};
  const SrpCollision::Vec3 to{550.5393692487451,1472.3956440312816,-248.2343353219623};
  const auto hit=SrpCollision::SweepSphereMesh(from,to,10.,*target);
  const bool legacy=std::getenv("SRP_R49_EXPECT_COUNTERCLOCKWISE_POSE")!=nullptr;
  REQUIRE(hit.verdict==(legacy?SrpCollision::Verdict::Clear:SrpCollision::Verdict::Blocked));
  // Wrong-facing casts still miss. No padding of projectile/controller/ray.
  REQUIRE(SrpCollision::SweepSphereMesh(from,{2*from.x-to.x,2*from.y-to.y,2*from.z-to.z},10.,*target).verdict==SrpCollision::Verdict::Clear);
  if(!legacy){
    REQUIRE(hit.hitFraction>0);REQUIRE(hit.hitFraction<1);
    SrpCollision::Query q;q.worldOrCell=33580089;q.from=from;q.radius=10.;
    q.to={from.x+(to.x-from.x)*hit.hitFraction,from.y+(to.y-from.y)*hit.hitFraction,from.z+(to.z-from.z)*hit.hitFraction};
    q.dynamics.worldOrCell=q.worldOrCell;q.dynamics.revision=2176;q.dynamics.complete=true;
    const auto trace=scene->Trace(q);INFO(trace.reason);
    REQUIRE(trace.verdict==SrpCollision::Verdict::Clear);
  }
  actor.ApplyChangeForm(before);
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
          "[srp-original-impact][srp-r46-melee][srp-r48-claws]")
{
  const bool claws = GENERATE(false, true);
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
  if (claws) { SetupClaws(server, player); server.SetDamageFormula(std::make_unique<TES5DamageFormula>()); }
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
  if (claws) impact["power"] = "werewolfClaws";
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
  if (claws) {
    const auto pending=listener->result.at("operationId").get<std::string>();
    REQUIRE(SrpOriginalImpact::Cancel(server,testUserId,pending).at("state")=="CANCELLED");
    WaitForSaved(server,player);
    Appearance human;human.raceId=0x13746;player.SetAppearance(&human);WaitForSaved(server,player);
    auto crossForm=impact;crossForm.erase("power");crossForm["session"]=negotiate();
    send(crossForm);
    REQUIRE(listener->result.at("operationId")==op);
    REQUIRE(listener->result.at("state")=="APPLIED");
    REQUIRE(cow.GetActorValues().healthPercentage==after);
  }
  listener->server = nullptr;
}

TEST_CASE("Original melee reconciles durable target damage after SIGKILL before acknowledgement",
          "[srp-r46-crash][srp-r48-claws][crash]")
{
  const bool claws = GENERATE(false, true);
  using Json = nlohmann::json;
  const auto path = std::filesystem::temp_directory_path() /
    ("srp-impact-crash-" + std::to_string(getpid()) + (claws ? "-claws" : "-human"));
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
      if (claws) { SetupClaws(server, attacker); server.SetDamageFormula(std::make_unique<TES5DamageFormula>()); }
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
        std::string(claws ? "claw-" : "hit-")+"0123456789abcdef0123456789abcdef").at("operationId").get<std::string>();
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

TEST_CASE("Only durable inactive original bodies can leave the untracked live set",
          "[srp-r49-inactive-physics]")
{
  auto& server = GetPartOne();
  AttachStorage(server);
  auto& cow = SetupCow(server);
  auto& player = SetupPlayer(server, cow);
  Isolate(server, cow, player);
  auto& world = server.worldState;
  // Original container in the cow's original cell. Geometry below is a
  // unit-only reference binding, not a claim about this barrel's real mesh.
  constexpr uint32_t bodyId = 0x4cc2d;
  auto& body = world.GetFormAt<MpObjectReference>(bodyId);
  const auto before = body.GetChangeForm();
  struct Restore {
    WorldState& world;
    MpObjectReference& body;
    MpChangeForm before;
    ~Restore() { world.InstallSrpCollisionScene(nullptr); body.ApplyChangeForm(before); }
  } restore{world, body, before};
  const auto cell = cow.GetCellOrWorld().ToFormId(world.espmFiles);
  REQUIRE(body.GetCellOrWorld() == cow.GetCellOrWorld());
  const auto desc = [&](uint32_t id) { return FormDesc::FromFormId(id, world.espmFiles).ToString(); };
  const auto pos = body.GetPos();
  const auto rot = body.GetAngle();
  auto binding = SyntheticReferenceBinding(body, world);
  auto audit = AuditConfig(cell, true, {bodyId});
  audit["physicalBaselineVerified"] = true;
  audit["allowDurableInactiveOriginalBodies"] = true;
  audit["dynamicAudit"]["untrackedPhysics"] = nlohmann::json::array({bodyId});
  const auto install = [&](const nlohmann::json& rows, const nlohmann::json& config) {
    auto scene = CreateSyntheticScene(cell, desc(cow.GetBaseId()), desc(cow.GetRaceId()),
      cow.GetPos(), desc(player.GetBaseId()), desc(player.GetRaceId()), false, false, 25, 0, rows);
    world.InstallSrpCollisionScene(scene, config);
    return scene;
  };
  const auto rows = nlohmann::json::array({binding});
  const auto scene = install(rows, audit);
  const auto frame = [&] { return world.CaptureSrpOriginalImpactFrame(player, cow); };
  body.Enable();
  REQUIRE_FALSE(frame());
  body.Disable();
  REQUIRE(world.GetChangeFormGeneration(bodyId) > 0);
  REQUIRE_FALSE(frame());
  WaitForSaved(server, body);
  if (std::getenv("SRP_R49_EXPECT_UNTRACKED_INACTIVE_REFUSAL")) {
    // The old native rejects a correctly saved disabled original body too.
    REQUIRE_FALSE(frame());
    return;
  }
  REQUIRE(frame());
  REQUIRE(frame()->scene == scene);
  REQUIRE(frame()->dynamics.complete);
  REQUIRE(scene->References().size() == 1); // Geometry is retained, never erased.

  auto invalid = audit;
  invalid.erase("allowDurableInactiveOriginalBodies");
  install(rows, invalid); REQUIRE_FALSE(frame());
  invalid["allowDurableInactiveOriginalBodies"] = false;
  install(rows, invalid); REQUIRE_FALSE(frame());
  invalid = audit;
  invalid["dynamicAudit"]["untrackedPhysics"] = nlohmann::json::array({bodyId, bodyId});
  install(rows, invalid); REQUIRE_FALSE(frame());
  for (const auto& id : nlohmann::json::array({0, -1, 1.5, 0x100000000ULL, cowId, 0x7546f})) {
    invalid = audit;
    invalid["dynamicAudit"]["untrackedPhysics"] = nlohmann::json::array({id});
    install(rows, invalid); REQUIRE_FALSE(frame());
  }
  invalid = audit;
  invalid["dynamicAudit"].erase("untrackedPhysics");
  install(rows, invalid); REQUIRE_FALSE(frame());
  install(nlohmann::json::array(), audit); REQUIRE_FALSE(frame());
  auto wrong = binding;
  wrong["base"] = desc(cow.GetBaseId());
  install(nlohmann::json::array({wrong}), audit); REQUIRE_FALSE(frame());
  install(rows, audit); REQUIRE(frame());

  body.SetPropertyValueDump("private.r49InactiveUnit", "1", false, false);
  REQUIRE_FALSE(frame());
  WaitForSaved(server, body); REQUIRE(frame());
  body.Enable(); REQUIRE_FALSE(frame());
  WaitForSaved(server, body); REQUIRE_FALSE(frame());
  body.Disable(); WaitForSaved(server, body); REQUIRE(frame());
  auto moved = pos; moved.x += 1;
  body.SetPos(moved); REQUIRE_FALSE(frame());
  WaitForSaved(server, body); REQUIRE_FALSE(frame());
  body.SetPos(pos); WaitForSaved(server, body); REQUIRE(frame());
  auto turned = rot; turned.z += 1;
  body.SetAngle(turned); REQUIRE_FALSE(frame());
  body.SetAngle(rot); WaitForSaved(server, body); REQUIRE(frame());
  body.SetCellOrWorld(FormDesc::Tamriel()); REQUIRE_FALSE(frame());
  body.SetCellOrWorld(cow.GetCellOrWorld()); WaitForSaved(server, body); REQUIRE(frame());
}

TEST_CASE("Cold disabled original bodies need a new durable acknowledgment",
          "[srp-r49-inactive-physics][srp-r49-inactive-cold]")
{
  constexpr uint32_t bodyId = 0x4cc2d;
  auto& first = GetPartOne();
  AttachStorage(first, std::filesystem::temp_directory_path() /
    ("r49-inactive-source-" + std::to_string(getpid())));
  auto& source = first.worldState.GetFormAt<MpObjectReference>(bodyId);
  // Use the public inventory writer to materialize the base container before
  // its saved snapshot is loaded into the second world.
  source.AddItem(0xf, 1);
  source.SetPropertyValueDump("private.r49InactiveUnit", "1", false, false);
  source.Disable();
  WaitForSaved(first, source);
  const auto saved = source.GetChangeForm();
  REQUIRE(saved.isDisabled);

  auto& resumed = GetPartOne();
  AttachStorage(resumed, std::filesystem::temp_directory_path() /
    ("r49-inactive-resumed-" + std::to_string(getpid())));
  auto& world = resumed.worldState;
  world.LoadChangeForm(saved, FormCallbacks::DoNothing());
  auto& cold = world.GetFormAt<MpObjectReference>(bodyId);
  REQUIRE(cold.IsDisabled());
  REQUIRE(world.GetChangeFormGeneration(bodyId) == 0);
  auto& cow = SetupCow(resumed);
  auto& player = SetupPlayer(resumed, cow);
  Isolate(resumed, cow, player);
  REQUIRE(world.GetChangeFormGeneration(bodyId) == 0);
  const auto cell = cow.GetCellOrWorld().ToFormId(world.espmFiles);
  const auto desc = [&](uint32_t id) { return FormDesc::FromFormId(id, world.espmFiles).ToString(); };
  auto scene = CreateSyntheticScene(cell, desc(cow.GetBaseId()), desc(cow.GetRaceId()), cow.GetPos(),
    desc(player.GetBaseId()), desc(player.GetRaceId()), false, false, 25, 0,
    nlohmann::json::array({SyntheticReferenceBinding(cold, world)}));
  auto audit = AuditConfig(cell, true, {bodyId});
  audit["physicalBaselineVerified"] = true;
  audit["allowDurableInactiveOriginalBodies"] = true;
  audit["dynamicAudit"]["untrackedPhysics"] = nlohmann::json::array({bodyId});
  world.InstallSrpCollisionScene(scene, audit);
  const auto frame = [&] { return world.CaptureSrpOriginalImpactFrame(player, cow); };
  REQUIRE_FALSE(frame());
  // Reassert the SAME metadata through the existing writer; generation zero
  // is never treated as an ACK just because the stored disabled flag is true.
  cold.SetPropertyValueDump("private.r49InactiveUnit", "1", false, false);
  REQUIRE(world.GetChangeFormGeneration(bodyId) > 0);
  REQUIRE_FALSE(frame());
  WaitForSaved(resumed, cold);
  if (std::getenv("SRP_R49_EXPECT_UNTRACKED_INACTIVE_REFUSAL")) REQUIRE_FALSE(frame());
  else REQUIRE(frame());
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

TEST_CASE("Original melee refuses expired hosts and scopes receipts across characters",
          "[srp-original-impact][srp-r46-melee]")
{
  auto& server = GetPartOne();
  AttachStorage(server);
  auto& target = SetupCow(server);
  auto& attacker = SetupPlayer(server, target);
  auto& other = SetupSecondPlayer(server, target);
  Isolate(server, target, attacker);
  ClearImpactState(other);
  WaitForSaved(server, other);
  auto& world = server.worldState;
  const auto desc = [&](uint32_t id) {
    return FormDesc::FromFormId(id, world.espmFiles).ToString();
  };
  const auto cell = target.GetCellOrWorld().ToFormId(world.espmFiles);
  world.InstallSrpCollisionScene(CreateSyntheticScene(cell, desc(target.GetBaseId()),
    desc(target.GetRaceId()), target.GetPos(), desc(attacker.GetBaseId()),
    desc(attacker.GetRaceId()), true), AuditConfig(cell, true));
  world.hosters[cowId] = secondPlayerId;
  const auto before = target.GetActorValues().healthPercentage;
  const auto first = SrpOriginalImpact::Begin(server, testUserId, cowId, "hit-scoped-host");
  const auto id = first.at("operationId").get<std::string>();
  WaitForSaved(server, attacker);
  server.serverState.Disconnect(secondUserId);
  REQUIRE_THROWS_WITH(SrpOriginalImpact::Apply(server, testUserId, id),
    "SRP_IMPACT_TARGET_HOST_SESSION_REQUIRED");
  REQUIRE(target.GetActorValues().healthPercentage == before);
  REQUIRE(SrpOriginalImpact::Cancel(server, testUserId, id).at("state") == "CANCELLED");
  WaitForSaved(server, attacker);
  world.hosters.erase(cowId);

  // The same connection now controls a different durable character.
  server.SetUserActor(testUserId, secondPlayerId);
  const auto next = SrpOriginalImpact::Begin(server, testUserId, cowId, "hit-scoped-host");
  const auto nextId = next.at("operationId").get<std::string>();
  REQUIRE(nextId != id);
  REQUIRE_THROWS_WITH(SrpOriginalImpact::Apply(server, testUserId, id),
    "SRP_IMPACT_OPERATION_NOT_ISSUED");
  REQUIRE(SrpOriginalImpact::Inspect(attacker).at("ledger").at("operations")
    .at(id).at("state") == "CANCELLED");
  REQUIRE(SrpOriginalImpact::Cancel(server, testUserId, nextId).at("state") == "CANCELLED");
  REQUIRE(target.GetActorValues().healthPercentage == before);
  REQUIRE(SrpOriginalImpact::Apply(server, testUserId, nextId).at("state") == "CANCELLED");
  WaitForSaved(server, other);
  const auto image = other.GetChangeForm();
  auto& restarted = GetPartOne();
  AttachStorage(restarted);
  restarted.worldState.LoadChangeForm(image, restarted.CreateFormCallbacks());
  auto& restored = restarted.worldState.GetFormAt<MpActor>(secondPlayerId);
  restarted.serverState.Connect(testUserId, "cancelled-retry-session");
  restarted.serverState.actorsMap.Set(testUserId, &restored);
  const auto retry = SrpOriginalImpact::Apply(restarted, testUserId, nextId);
  REQUIRE(retry.at("state") == "CANCELLED");
  REQUIRE(retry.at("saved") == false);
  WaitForSaved(restarted, restored);
  REQUIRE(SrpOriginalImpact::Apply(restarted, testUserId, nextId).at("saved") == true);
  REQUIRE_FALSE(restarted.worldState.LookupFormByIdNoLoad(cowId));
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
