#include "TestUtils.hpp"

extern espm::Loader& GetEspmLoader();
#include "ActionListener.h"
#include "HitMessage.h"

TEST_CASE("First host attempt initializes its movement timestamp", "[PartOne][HostBounds]")
{
  PartOne partOne;
  partOne.AttachEspm(&GetEspmLoader());
  partOne.worldState.bannedEspmCharacterRaceIds.clear();
  DoConnect(partOne, 0);
  partOne.CreateActor(0xff000001, { 1.f, 2.f, 3.f }, 180.f, 0x3c);
  partOne.SetUserActor(0, 0xff000001);
  partOne.CreateActor(0xff000002, { 1.f, 2.f, 3.f }, 180.f, 0x3c);
  const auto remote = std::dynamic_pointer_cast<MpActor>(partOne.worldState.LookupFormById(0xff000002));
  REQUIRE(remote);
  REQUIRE(partOne.worldState.lastMovUpdateByIdx.empty());

  DoMessage(partOne, 0, {{"t", MsgType::Host}, {"remoteId", 0xff000002}});

  REQUIRE(partOne.worldState.lastMovUpdateByIdx.size() > remote->GetIdx());
  REQUIRE(partOne.worldState.lastMovUpdateByIdx.at(remote->GetIdx()).has_value());
  REQUIRE(partOne.worldState.hosters.at(0xff000002) == 0xff000001);
  DoUpdateMovement(partOne, 0xff000002, 0);
  REQUIRE(partOne.worldState.lastMovUpdateByIdx.at(remote->GetIdx()).has_value());
}

TEST_CASE("Higher actor index preserves earlier movement timestamps", "[PartOne][HostBounds]")
{
  PartOne partOne;
  partOne.AttachEspm(&GetEspmLoader());
  partOne.worldState.bannedEspmCharacterRaceIds.clear();
  DoConnect(partOne, 0);
  partOne.CreateActor(0xff000001, { 1.f, 2.f, 3.f }, 180.f, 0x3c);
  partOne.SetUserActor(0, 0xff000001);
  DoUpdateMovement(partOne, 0xff000001, 0);
  const auto first = partOne.worldState.lastMovUpdateByIdx;
  for (uint32_t i = 2; i <= 33; ++i)
    partOne.CreateActor(0xff000000 + i, { 1.f, 2.f, 3.f }, 180.f, 0x3c);
  const auto remote = std::dynamic_pointer_cast<MpActor>(partOne.worldState.LookupFormById(0xff000021));
  REQUIRE(remote);
  REQUIRE(partOne.worldState.lastMovUpdateByIdx.size() <= remote->GetIdx());

  DoMessage(partOne, 0, {{"t", MsgType::Host}, {"remoteId", 0xff000021}});

  REQUIRE(partOne.worldState.lastMovUpdateByIdx.size() > remote->GetIdx());
  REQUIRE(partOne.worldState.lastMovUpdateByIdx.at(remote->GetIdx()).has_value());
  for (size_t i = 0; i < first.size(); ++i)
    REQUIRE(partOne.worldState.lastMovUpdateByIdx.at(i) == first.at(i));
  DoUpdateMovement(partOne, 0xff000021, 0);
  REQUIRE(partOne.worldState.hosters.at(0xff000021) == 0xff000001);
}

TEST_CASE("Host bounds fix does not permit hosting another player", "[PartOne][HostBounds]")
{
  PartOne partOne;
  partOne.AttachEspm(&GetEspmLoader());
  partOne.worldState.bannedEspmCharacterRaceIds.clear();
  for (uint32_t i = 0; i < 2; ++i) {
    DoConnect(partOne, i);
    partOne.CreateActor(0xff000001 + i, { 1.f, 2.f, 3.f }, 180.f, 0x3c);
    partOne.SetUserActor(i, 0xff000001 + i);
  }
  const auto before = partOne.worldState.lastMovUpdateByIdx;

  DoMessage(partOne, 0, {{"t", MsgType::Host}, {"remoteId", 0xff000002}});

  REQUIRE(partOne.worldState.lastMovUpdateByIdx == before);
  REQUIRE(partOne.worldState.hosters.count(0xff000002) == 0);
}


TEST_CASE("Repeated host assignment and movement remain bounded", "[PartOne][HostBounds]")
{
  for (uint32_t boot = 0; boot < 32; ++boot) {
    PartOne partOne;
  partOne.AttachEspm(&GetEspmLoader());
  partOne.worldState.bannedEspmCharacterRaceIds.clear();
    DoConnect(partOne, 0);
    partOne.CreateActor(0xff000001, {1.f, 2.f, 3.f}, 180.f, 0x3c);
    partOne.SetUserActor(0, 0xff000001);
    for (uint32_t n = 2; n <= 65; ++n) {
      const uint32_t id = 0xff000000 + n;
      partOne.CreateActor(id, {1.f, 2.f, 3.f}, 180.f, 0x3c);
      const auto remote = std::dynamic_pointer_cast<MpActor>(partOne.worldState.LookupFormById(id));
      REQUIRE(remote);
      for (uint32_t repeat = 0; repeat < 8; ++repeat) {
        DoMessage(partOne, 0, {{"t", MsgType::Host}, {"remoteId", id}});
        DoUpdateMovement(partOne, id, 0);
        REQUIRE(partOne.worldState.hosters.at(id) == 0xff000001);
        REQUIRE(partOne.worldState.lastMovUpdateByIdx.at(remote->GetIdx()).has_value());
      }
    }
    DoDisconnect(partOne, 0);
    DoConnect(partOne, 0);
    partOne.SetUserActor(0, 0xff000001);
    DoUpdateMovement(partOne, 0xff000001, 0);
  }
}

TEST_CASE("Invalid movement index leaves timestamp storage unchanged", "[PartOne][HostBounds]")
{
  PartOne partOne;
  partOne.AttachEspm(&GetEspmLoader());
  partOne.worldState.bannedEspmCharacterRaceIds.clear();
  DoConnect(partOne, 0);
  partOne.CreateActor(0xff000001, {1.f, 2.f, 3.f}, 180.f, 0x3c);
  partOne.SetUserActor(0, 0xff000001);
  const auto before = partOne.worldState.lastMovUpdateByIdx;
  for (uint32_t idx : {0x7fffffffU, 0x80000000U, 0xffffffffU}) {
    auto message = jMovement;
    message["idx"] = idx;
    DoMessage(partOne, 0, message);
    REQUIRE(partOne.worldState.lastMovUpdateByIdx == before);
  }
}

TEST_CASE("Unassigned connected player cannot host NPC", "[PartOne][HostBounds]")
{
  PartOne partOne;
  partOne.AttachEspm(&GetEspmLoader());
  partOne.worldState.bannedEspmCharacterRaceIds.clear();
  DoConnect(partOne, 0);
  partOne.CreateActor(0xff000002, {1.f, 2.f, 3.f}, 180.f, 0x3c);
  const auto before = partOne.worldState.lastMovUpdateByIdx;
  DoMessage(partOne, 0, {{"t", MsgType::Host}, {"remoteId", 0xff000002}});
  REQUIRE(partOne.worldState.hosters.count(0xff000002) == 0);
  REQUIRE(partOne.worldState.lastMovUpdateByIdx == before);
}

TEST_CASE("Missing and despawned NPC hosting is rejected", "[PartOne][HostBounds]")
{
  PartOne partOne;
  partOne.AttachEspm(&GetEspmLoader());
  partOne.worldState.bannedEspmCharacterRaceIds.clear();
  DoConnect(partOne, 0);
  partOne.CreateActor(0xff000001, {1.f, 2.f, 3.f}, 180.f, 0x3c);
  partOne.SetUserActor(0, 0xff000001);
  const auto before = partOne.worldState.lastMovUpdateByIdx;
  for (uint32_t id : {0U, 0xff000002U, 0xffffffffU}) {
    REQUIRE_NOTHROW(DoMessage(partOne, 0, {{"t", MsgType::Host}, {"remoteId", id}}));
    REQUIRE(partOne.worldState.hosters.count(id) == 0);
    REQUIRE(partOne.worldState.lastMovUpdateByIdx == before);
  }
  partOne.CreateActor(0xff000002, {1.f, 2.f, 3.f}, 180.f, 0x3c);
  partOne.worldState.DestroyForm(0xff000002);
  REQUIRE_NOTHROW(DoMessage(partOne, 0, {{"t", MsgType::Host}, {"remoteId", 0xff000002}}));
  REQUIRE(partOne.worldState.hosters.count(0xff000002) == 0);
  REQUIRE(partOne.worldState.lastMovUpdateByIdx == before);
}

TEST_CASE("Disconnected player hosting is rejected", "[PartOne][HostBounds]")
{
  PartOne partOne;
  partOne.AttachEspm(&GetEspmLoader());
  partOne.worldState.bannedEspmCharacterRaceIds.clear();
  DoConnect(partOne, 0);
  partOne.CreateActor(0xff000001, {1.f, 2.f, 3.f}, 180.f, 0x3c);
  partOne.SetUserActor(0, 0xff000001);
  partOne.CreateActor(0xff000002, {1.f, 2.f, 3.f}, 180.f, 0x3c);
  DoDisconnect(partOne, 0);
  const auto before = partOne.worldState.lastMovUpdateByIdx;
  REQUIRE_NOTHROW(DoMessage(partOne, 0, {{"t", MsgType::Host}, {"remoteId", 0xff000002}}));
  REQUIRE(partOne.worldState.hosters.count(0xff000002) == 0);
  REQUIRE(partOne.worldState.lastMovUpdateByIdx == before);
}

TEST_CASE("NPC host reassignment and deferred despawn reject stale identity", "[PartOne][HostBounds]")
{
  PartOne partOne;
  partOne.AttachEspm(&GetEspmLoader());
  partOne.worldState.bannedEspmCharacterRaceIds.clear();
  for (uint32_t user = 0; user < 2; ++user) {
    DoConnect(partOne, user);
    partOne.CreateActor(0xff000001 + user, {1.f, 2.f, 3.f}, 180.f, 0x3c);
    partOne.SetUserActor(user, 0xff000001 + user);
  }
  constexpr uint32_t npcId = 0xff000010;
  partOne.CreateActor(npcId, {1.f, 2.f, 3.f}, 180.f, 0x3c);
  const auto npc = std::dynamic_pointer_cast<MpActor>(partOne.worldState.LookupFormById(npcId));
  REQUIRE(npc);
  for (uint32_t n = 0; n < 64; ++n) {
    const auto user = n % 2;
    DoMessage(partOne, user, {{"t", MsgType::Host}, {"remoteId", npcId}});
    REQUIRE(partOne.worldState.hosters.at(npcId) == 0xff000001 + user);
    partOne.worldState.lastMovUpdateByIdx.at(npc->GetIdx()) =
      std::chrono::system_clock::now() - std::chrono::seconds(3);
  }
  partOne.worldState.DestroyForm(npcId);
  partOne.CreateActor(npcId, {1.f, 2.f, 3.f}, 180.f, 0x3c);
  REQUIRE(partOne.worldState.LookupFormByIdNoLoad(npcId) != npc);
  std::this_thread::sleep_for(std::chrono::milliseconds(1100));
  REQUIRE_NOTHROW(partOne.worldState.Tick());
}

PartOne& GetPartOne();
TEST_CASE("Hosted NPC combat death respawn and player reconnect remain bounded",
          "[HostBoundsLifecycle]")
{
  PartOne& p = GetPartOne();
  constexpr uint32_t playerId = 0xff0000e1;
  constexpr uint32_t npcId = 0xff0000e2;
  DoConnect(p, 0);
  p.CreateActor(playerId, {0.f, 0.f, 0.f}, 0.f, 0x3c);
  p.SetUserActor(0, playerId);
  p.CreateActor(npcId, {0.f, 0.f, 0.f}, 180.f, 0x3c);
  auto& player = p.worldState.GetFormAt<MpActor>(playerId);
  auto& npc = p.worldState.GetFormAt<MpActor>(npcId);
  DoMessage(p, 0, {{"t", MsgType::Host}, {"remoteId", npcId}});
  REQUIRE(p.worldState.hosters.at(npcId) == playerId);
  DoUpdateMovement(p, npcId, 0);
  Inventory::ExtraData worn;
  worn.worn_ = true;
  Equipment equipment;
  equipment.inv.entries.push_back(Inventory::Entry(0x1397e, 1, worn));
  player.AddItem(0x1397e, 1);
  player.SetEquipment(equipment);
  RawMessageData raw;
  raw.userId = 0;
  HitMessage hit;
  hit.data.target = npcId;
  hit.data.aggressor = 0x14;
  hit.data.source = 0x1397e;
  for (uint32_t cycle = 0; cycle < 16; ++cycle) {
    // Respawn uses the engine spawn point; keep each combat cycle co-located
    // without asserting anything about collision/grounding.
    npc.SetCellOrWorld(player.GetCellOrWorld());
    npc.SetPos(player.GetPos());
    const auto before = npc.GetChangeForm().actorValues.healthPercentage;
    player.SetLastHitTime(npcId, std::chrono::steady_clock::now() - std::chrono::seconds(10));
    REQUIRE_NOTHROW(p.GetActionListener().OnHit(raw, hit));
    REQUIRE(npc.GetChangeForm().actorValues.healthPercentage < before);
    npc.Kill();
    REQUIRE(npc.IsDead());
    REQUIRE(npc.GetChangeForm().actorValues.healthPercentage == 0.f);
    npc.Respawn();
    REQUIRE_FALSE(npc.IsDead());
    REQUIRE(npc.GetChangeForm().actorValues.healthPercentage == 1.f);
    DoDisconnect(p, 0);
    DoConnect(p, 0);
    p.SetUserActor(0, playerId);
    DoUpdateMovement(p, playerId, 0);
    DoUpdateMovement(p, npcId, 0);
    REQUIRE(p.worldState.lastMovUpdateByIdx.at(npc.GetIdx()).has_value());
  }
  p.DestroyActor(npcId);
  p.DestroyActor(playerId);
  DoDisconnect(p, 0);
}
