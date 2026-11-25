#include "TestUtils.hpp"
#include "CreateActorMessage.h"
#include "UpdatePropertyMessage.h"
#include "HitMessage.h"
#include "SpellCastMessage.h"
#include "HostMessage.h"
#include "ActionListener.h"
#include <algorithm>
#include <catch2/catch_all.hpp>
#include <cstdlib>

PartOne& GetPartOne();

namespace {
MpObjectReference& CreateMpObjectReference_(WorldState& worldState,
                                            uint32_t id)
{
  auto refr = std::make_unique<MpObjectReference>(
    LocationalData(), FormCallbacks::DoNothing(), 0, "CONT");
  worldState.AddForm(std::move(refr), id);
  return worldState.GetFormAt<MpObjectReference>(id);
}
}

TEST_CASE("Disable makes ref invisible", "[ObjectReference]")
{

  PartOne p;

  auto& ref = CreateMpObjectReference_(p.worldState, 0xff000000);
  ref.SetCellOrWorld(FormDesc::Tamriel());

  p.CreateActor(0xff000001, { 0, 0, 0 }, 0, 0x3c);
  DoConnect(p, 0);
  p.SetUserActor(0, 0xff000001);
  DoUpdateMovement(p, 0xff000001, 0);

  auto& ac = p.worldState.GetFormAt<MpActor>(0xff000001);

  REQUIRE(ref.GetListeners() == std::set<MpObjectReference*>{ &ac });
  ref.Disable();
  REQUIRE(ref.GetListeners() == std::set<MpObjectReference*>{});

  ref.SetPos(ref.GetPos());
  ref.SetPos(ref.GetPos() + NiPoint3{ 10000, 10000, 10000 });
  REQUIRE(ref.GetListeners() == std::set<MpObjectReference*>{});
  ref.SetPos(ref.GetPos() - NiPoint3{ 10000, 10000, 10000 });

  ref.Enable();
  REQUIRE(ref.GetListeners() == std::set<MpObjectReference*>{ &ac });
}

TEST_CASE("Disabled original nonactors keep the same warm cold and reconnect projection",
          "[ObjectReference][srp-r49-disabled-interest]")
{
  const bool baseline = std::getenv("SRP_R49_EXPECT_DISABLED_COLD_REFUSAL") != nullptr;
  // An original barrel and the exact three R49 feeding-scene objects.
  for (const uint32_t id : { 0x4cc2du, 0x6b78bu, 0x8eab4u, 0x108a48u }) {
    INFO("original reference " << std::hex << id);
    auto& warm = GetPartOne();
    auto& ref = warm.worldState.GetFormAt<MpObjectReference>(id);
    REQUIRE_FALSE(ref.AsActor());
    const auto pos = ref.GetPos();
    const auto cell = ref.GetCellOrWorld().ToFormId(warm.worldState.espmFiles);
    ref.Disable();
    const auto image = ref.GetChangeForm();
    REQUIRE(image.isDisabled);
    DoConnect(warm, 0);
    warm.CreateActor(0xff000000, pos, 0, cell);
    warm.SetUserActor(0, 0xff000000);
    auto messages = FindRefrMessage<CreateActorMessage>(warm, id).filteredMessages;
    REQUIRE(messages.size() == 1);
    REQUIRE(messages.front().props.isDisabled == true);
    DoDisconnect(warm, 0);

    auto& cold = GetPartOne();
    cold.worldState.LoadChangeForm(image, cold.CreateFormCallbacks());
    auto& restored = cold.worldState.GetFormAt<MpObjectReference>(id);
    REQUIRE(restored.IsDisabled());
    REQUIRE_FALSE(restored.IsDeleted());
    REQUIRE(restored.GetPos() == pos);
    cold.Messages().clear();
    DoConnect(cold, 0);
    cold.CreateActor(0xff000000, pos, 0, cell);
    cold.SetUserActor(0, 0xff000000);
    messages = FindRefrMessage<CreateActorMessage>(cold, id).filteredMessages;
    if (baseline) {
      REQUIRE(messages.empty());
      DoDisconnect(cold, 0);
      continue;
    }
    REQUIRE(messages.size() == 1);
    REQUIRE(messages.front().props.isDisabled == true);
    DoDisconnect(cold, 0);
    cold.Messages().clear();
    DoConnect(cold, 1);
    cold.CreateActor(0xff000001, pos, 0, cell);
    cold.SetUserActor(1, 0xff000001);
    messages = FindRefrMessage<CreateActorMessage>(cold, id).filteredMessages;
    REQUIRE(messages.size() == 1);
    REQUIRE(messages.front().props.isDisabled == true);
    REQUIRE(restored.IsDisabled());
    REQUIRE(restored.GetChangeForm().inv.ToJson() == image.inv.ToJson());
    DoDisconnect(cold, 1);
  }
}

TEST_CASE("Disabled dynamic references actors and deleted originals stay excluded",
          "[ObjectReference][srp-r49-disabled-interest]")
{
  PartOne part;
  auto& dynamic = CreateMpObjectReference_(part.worldState, 0xff000010);
  dynamic.SetCellOrWorld(FormDesc::Tamriel());
  dynamic.Disable();
  part.CreateActor(0xff000011, { 0, 0, 0 }, 0, 0x3c);
  auto& hiddenActor = part.worldState.GetFormAt<MpActor>(0xff000011);
  hiddenActor.Disable();
  DoConnect(part, 0);
  part.CreateActor(0xff000000, { 0, 0, 0 }, 0, 0x3c);
  part.SetUserActor(0, 0xff000000);
  REQUIRE(dynamic.GetListeners().empty());
  REQUIRE(hiddenActor.GetListeners().empty());
  REQUIRE(FindRefrMessage<CreateActorMessage>(part, 0xff000010).filteredMessages.empty());
  REQUIRE(FindRefrMessage<CreateActorMessage>(part, 0xff000011).filteredMessages.empty());
  DoDisconnect(part, 0);

  auto& source = GetPartOne();
  auto image = source.worldState.GetFormAt<MpObjectReference>(0x4cc2d).GetChangeForm();
  image.isDisabled = true;
  image.isDeleted = true;
  auto& cold = GetPartOne();
  cold.worldState.LoadChangeForm(image, cold.CreateFormCallbacks());
  DoConnect(cold, 0);
  cold.CreateActor(0xff000000, image.position, 0,
    image.worldOrCellDesc.ToFormId(cold.worldState.espmFiles));
  cold.SetUserActor(0, 0xff000000);
  REQUIRE(FindRefrMessage<CreateActorMessage>(cold, 0x4cc2d).filteredMessages.empty());
  DoDisconnect(cold, 0);
}

TEST_CASE("Disabled original actors project warm cold and reconnect without host authority",
          "[ObjectReference][srp-r49-disabled-actor-interest]")
{
  const bool baseline = std::getenv("SRP_R49_EXPECT_DISABLED_ACTOR_COLD_REFUSAL") != nullptr;
  constexpr uint32_t id = 0x523a9, playerId = 0xff000000;
  auto& warm = GetPartOne();
  warm.worldState.npcEnabled = true;
  warm.worldState.npcSettings["Skyrim.esm"].spawnInInterior = true;
  auto& actor = warm.worldState.GetFormAt<MpActor>(id);
  warm.worldState.npcEnabled = false;
  Equipment spells; spells.leftSpell = 0x12fcd; actor.SetEquipment(spells);
  const auto pos = actor.GetPos();
  const auto cell = actor.GetCellOrWorld().ToFormId(warm.worldState.espmFiles);
  const auto before = actor.GetChangeForm();
  DoConnect(warm, 0);
  warm.CreateActor(playerId, pos, 0, cell);
  warm.SetUserActor(0, playerId);
  auto& player = warm.worldState.GetFormAt<MpActor>(playerId);
  REQUIRE(actor.GetListeners().count(&player) == 1);
  if (!baseline) {
    const auto initial = FindRefrMessageIdx<CreateActorMessage>(warm, actor.GetIdx()).filteredMessages;
    REQUIRE(initial.size() == 1);
    REQUIRE(initial.front().props.isDisabled == false);
    REQUIRE(initial.front().refrId == uint64_t(id) + 0x100000000);
  }
  warm.worldState.hosters[id] = playerId;
  warm.Messages().clear();
  actor.Disable();
  REQUIRE(actor.IsDisabled());
  const auto image = actor.GetChangeForm();
  if (baseline) {
    REQUIRE(actor.GetListeners().empty());
  } else {
    REQUIRE(actor.GetListeners().count(&player) == 1);
    REQUIRE(warm.worldState.hosters.count(id) == 0);
    REQUIRE_FALSE(warm.worldState.CanSrpNpcHost(actor, player));
    const auto delta = FindRefrMessage<UpdatePropertyMessage>(warm, id).filteredMessages;
    REQUIRE(std::any_of(delta.begin(), delta.end(), [](const auto& msg) {
      return msg.propName == "isDisabled" && msg.dataDump == "true";
    }));
    RawMessageData raw; raw.userId = 0;
    HostMessage host; host.remoteId = uint64_t(id) + 0x100000000;
    warm.GetActionListener().OnHostAttempt(raw, host);
    REQUIRE(warm.worldState.hosters.count(id) == 0);
    // Even an old, stale host map must not give an inactive actor combat.
    warm.worldState.hosters[id] = playerId;
    const auto health = player.GetActorValues().healthPercentage;
    HitMessage hit; hit.data.aggressor = id; hit.data.target = 0x14;
    hit.data.source = 0x1f4;
    warm.GetActionListener().OnHit(raw, hit);
    REQUIRE(player.GetActorValues().healthPercentage == health);
    const auto targetHealth = actor.GetActorValues().healthPercentage;
    hit.data.aggressor = 0x14; hit.data.target = id;
    warm.GetActionListener().OnHit(raw, hit);
    REQUIRE(actor.GetActorValues().healthPercentage == targetHealth);
    SpellCastMessage cast; cast.data.caster = id; cast.data.target = 0x14;
    cast.data.spell = 0x12fcd;
    warm.Messages().clear();
    REQUIRE_NOTHROW(warm.GetActionListener().OnSpellCast(raw, cast));
    REQUIRE(warm.Messages().empty());
    actor.Enable();
    REQUIRE_FALSE(actor.IsDisabled());
    const auto enabled = FindRefrMessage<UpdatePropertyMessage>(warm, id).filteredMessages;
    REQUIRE(std::any_of(enabled.begin(), enabled.end(), [](const auto& msg) {
      return msg.propName == "isDisabled" && msg.dataDump == "false";
    }));
    REQUIRE(actor.GetChangeForm().inv.ToJson() == before.inv.ToJson());
  }
  DoDisconnect(warm, 0);
  auto& cold = GetPartOne();
  cold.worldState.npcEnabled = true;
  cold.worldState.npcSettings["Skyrim.esm"].spawnInInterior = true;
  cold.worldState.LoadChangeForm(image, cold.CreateFormCallbacks());
  auto& restored = cold.worldState.GetFormAt<MpActor>(id);
  cold.worldState.npcEnabled = false;
  REQUIRE(restored.IsDisabled());
  REQUIRE(restored.GetPos() == pos);
  for (const auto user : {0, 1}) {
    cold.Messages().clear(); DoConnect(cold, user);
    cold.CreateActor(playerId + user, pos, 0, cell);
    cold.SetUserActor(user, playerId + user);
    const auto messages = FindRefrMessageIdx<CreateActorMessage>(cold, restored.GetIdx()).filteredMessages;
    if (baseline) REQUIRE(messages.empty());
    else {
      REQUIRE(messages.size() == 1);
      REQUIRE(messages.front().props.isDisabled == true);
      REQUIRE(messages.front().refrId == uint64_t(id) + 0x100000000);
      REQUIRE(messages.front().baseId == restored.GetBaseId());
      REQUIRE_FALSE(cold.worldState.CanSrpNpcHost(restored, cold.worldState.GetFormAt<MpActor>(playerId + user)));
      REQUIRE(restored.GetChangeForm().inv.ToJson() == image.inv.ToJson());
    }
    DoDisconnect(cold, user);
  }
  if (!baseline) {
    auto enabledImage = image; enabledImage.isDisabled = false;
    auto& enabled = GetPartOne();
    enabled.worldState.npcEnabled = true;
    enabled.worldState.npcSettings["Skyrim.esm"].spawnInInterior = true;
    enabled.worldState.LoadChangeForm(enabledImage, enabled.CreateFormCallbacks());
    auto& enabledActor = enabled.worldState.GetFormAt<MpActor>(id);
    enabled.worldState.npcEnabled = false;
    DoConnect(enabled, 0); enabled.CreateActor(playerId, pos, 0, cell); enabled.SetUserActor(0, playerId);
    const auto messages = FindRefrMessageIdx<CreateActorMessage>(enabled, enabledActor.GetIdx()).filteredMessages;
    REQUIRE(messages.size() == 1);
    REQUIRE(messages.front().props.isDisabled == false);
    DoDisconnect(enabled, 0);
  }
}
