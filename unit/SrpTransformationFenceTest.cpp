#include "SrpTransformationFence.h"
#include "ActionListener.h"
#include "RawMessageData.h"
#include "TestUtils.hpp"
#include "ChangeValuesMessage.h"
#include "HitMessage.h"
#include "UpdateEquipmentMessage.h"
#include <catch2/catch_all.hpp>

PartOne& GetPartOne();

TEST_CASE("Transformation fence preserves native resources and equipment packets", "[srp-transformation]")
{
  auto& server = GetPartOne();
  constexpr uint32_t id = 0xff0000b1;
  constexpr Networking::UserId user = 21;
  DoConnect(server, user);
  server.CreateActor(id, {0, 0, 0}, 0, 0x3c, 812);
  server.SetUserActor(user, id);
  auto& actor = server.worldState.GetFormAt<MpActor>(id);
  auto values = actor.GetActorValues();
  values.healthPercentage = .4f; values.magickaPercentage = .3f; values.staminaPercentage = .2f;
  actor.SetActorValues(values);
  actor.SetPropertyValueDump("private.srpTransformationV1", "{\"schemaVersion\":1,\"fenced\":true}", false, false);
  const auto before = actor.GetChangeForm();
  RawMessageData raw; raw.userId = user;
  ChangeValuesMessage change; change.data.health = 1; change.data.magicka = 1; change.data.stamina = 1;
  server.GetActionListener().OnChangeValues(raw, change);
  REQUIRE(actor.GetActorValues().ToTuple() == before.actorValues.ToTuple());
  UpdateEquipmentMessage equipment;
  equipment.data.numChanges = before.equipment.numChanges + 1;
  server.GetActionListener().OnUpdateEquipment(raw, equipment);
  REQUIRE(actor.GetEquipment() == before.equipment);

  // The blocked hit must not materialize a cold original target as a side effect.
  HitMessage hit; hit.data.aggressor = 0x14; hit.data.target = 0x10ebaf;
  hit.data.source = 0x1f4;
  const auto held = server.worldState.LookupFormByIdNoLoad(hit.data.target);
  server.GetActionListener().OnHit(raw, hit);
  REQUIRE(server.worldState.LookupFormByIdNoLoad(hit.data.target) == held);

  actor.SetPropertyValueDump("private.srpTransformationV1", "{\"schemaVersion\":1,\"fenced\":false}", false, false);
  change.data.health = .2f; change.data.magicka = .1f; change.data.stamina = .1f;
  server.GetActionListener().OnChangeValues(raw, change);
  REQUIRE(actor.GetActorValues().healthPercentage == .2f);
  REQUIRE(actor.GetActorValues().magickaPercentage == .1f);
  REQUIRE(actor.GetActorValues().staminaPercentage == .1f);
  server.DestroyActor(id); DoDisconnect(server, user);
}

TEST_CASE("Unknown transformation receipts fail closed and ordinary actors remain unchanged", "[srp-transformation]")
{
  auto& server = GetPartOne();
  constexpr uint32_t id = 0xff0000b2;
  server.CreateActor(id, {0, 0, 0}, 0, 0x3c, 813);
  auto& actor = server.worldState.GetFormAt<MpActor>(id);
  REQUIRE_FALSE(SrpTransformationFenced(actor));
  for (const auto* value : {"null", "{}", "{\"schemaVersion\":2,\"fenced\":false}",
                            "{\"schemaVersion\":1,\"fenced\":1}", "{\"schemaVersion\":\"1\",\"fenced\":false}"}) {
    actor.SetPropertyValueDump("private.srpTransformationV1", value, false, false);
    REQUIRE(SrpTransformationFenced(actor));
  }
  server.DestroyActor(id);
}
