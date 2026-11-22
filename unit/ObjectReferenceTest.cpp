#include "TestUtils.hpp"
#include "CreateActorMessage.h"
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
