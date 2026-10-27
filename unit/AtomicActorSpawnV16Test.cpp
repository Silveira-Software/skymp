#include "TestUtils.hpp"
#include "AtomicActorSpawnV16.h"
#include "CreateActorMessage.h"
#include "script_classes/PapyrusObjectReference.h"
#include "script_objects/EspmGameObject.h"
#include <limits>
#include <iostream>

extern espm::Loader& GetEspmLoader();

TEST_CASE("V16 atomic first publication contains final transform and base", "[V16Atomic][espm]")
{
  PartOne p;
  p.AttachEspm(&GetEspmLoader());
  p.CreateActor(0xff000010, {1000,1000,100}, 0, 0x3c);
  DoConnect(p,0); p.SetUserActor(0,0xff000010);
  // A second real mock-network recipient at the final ground ensures the
  // publication is observed even when the distant player is outside the grid.
  p.CreateActor(0xff000011, {9000,8000,250}, 0, 0x3c);
  DoConnect(p,1); p.SetUserActor(1,0xff000011);
  size_t observed=0;
  p.SetOnActorStreamIn([&](const MpActor& a,const MpObjectReference&,CreateActorMessage&) {
    if(a.GetBaseId()!=0x23ab8) return;
    ++observed;
    REQUIRE(a.GetPos().x==9000); REQUIRE(a.GetPos().y==8000); REQUIRE(a.GetPos().z==250);
    REQUIRE(a.GetAngle().z==90); REQUIRE(a.GetCellOrWorld().ToFormId(p.worldState.espmFiles)==0x3c);
  });
  const auto id=PlaceActorAtV16Native(p,0x23ab8,0x3c,{9000,8000,250},{0,0,90});
  const auto messages=FindRefrMessage<CreateActorMessage>(p,id);
  REQUIRE(observed>0); REQUIRE_FALSE(messages.filteredMessages.empty());
  for(const auto& msg: messages.filteredMessages) {
    REQUIRE((msg.transform.pos==std::array<float,3>{9000,8000,250}));
    REQUIRE((msg.transform.rot==std::array<float,3>{0,0,90}));
    REQUIRE(msg.transform.worldOrCell==0x3c); REQUIRE(msg.baseId==0x23ab8);
  }
  auto& actor=p.worldState.GetFormAt<MpActor>(id);
  REQUIRE(actor.GetPos().x==9000); REQUIRE(actor.GetBaseId()==0x23ab8);
  std::cout << "V16_FIRST_PUBLICATION_TRACE " << nlohmann::json{
    {"playerPosition",{1000,1000,100}},{"requestedGround",{9000,8000,250}},
    {"firstPacketPosition",messages.filteredMessages.front().transform.pos},
    {"firstPacketRotation",messages.filteredMessages.front().transform.rot},
    {"firstPacketWorld",messages.filteredMessages.front().transform.worldOrCell},
    {"firstPacketBase",messages.filteredMessages.front().baseId},
    {"reference",id},{"streamInCallbacks",observed},
    {"networkMessages",messages.filteredMessages.size()}}.dump() << std::endl;
}

TEST_CASE("V16 atomic validates NPC and world before any registration", "[V16Atomic][espm]")
{
  PartOne p;p.AttachEspm(&GetEspmLoader());
  REQUIRE_THROWS_WITH(PlaceActorAtV16Native(p,0x12eb7,0x3c,{1,2,3},{}),"v16_spawn_invalid_npc_base");
  REQUIRE_THROWS_WITH(PlaceActorAtV16Native(p,0x23ab8,0x12eb7,{1,2,3},{}),"v16_spawn_invalid_world_or_cell");
  REQUIRE_THROWS_WITH(PlaceActorAtV16Native(p,0x23ab8,0x3c,{std::numeric_limits<float>::quiet_NaN(),2,3},{}),"v16_spawn_nonfinite_transform");
  REQUIRE(p.Messages().empty());
}

TEST_CASE("V16 atomic supports exact cell and complete rotation", "[V16Atomic][espm]")
{
  PartOne p;p.AttachEspm(&GetEspmLoader());
  const auto id=PlaceActorAtV16Native(p,0x23ab8,0x371de,{100,200,300},{10,20,30});
  auto& actor=p.worldState.GetFormAt<MpActor>(id);
  REQUIRE(actor.GetCellOrWorld().ToFormId(p.worldState.espmFiles)==0x371de);
  REQUIRE(actor.GetPos().x==100);REQUIRE(actor.GetPos().y==200);REQUIRE(actor.GetPos().z==300);
  REQUIRE(actor.GetAngle().x==10);REQUIRE(actor.GetAngle().y==20);REQUIRE(actor.GetAngle().z==30);
}

TEST_CASE("V16 leaves legacy createActor and PlaceAtMe transforms unchanged", "[V16Atomic][espm]")
{
  PartOne p;p.AttachEspm(&GetEspmLoader());
  const auto player=p.CreateActor(0xff000010,{1000,1000,100},45,0x3c);
  DoConnect(p,0);p.SetUserActor(0,player);
  auto& anchor=p.worldState.GetFormAt<MpActor>(player);
  REQUIRE(anchor.GetBaseId()==7);REQUIRE(anchor.GetPos().x==1000);
  EspmGameObject base(GetEspmLoader().GetBrowser().LookupById(0x23ab8));
  const auto value=PapyrusObjectReference().PlaceAtMe(anchor.ToVarValue(),{VarValue(&base),VarValue(1),VarValue(false),VarValue(false)});
  REQUIRE(value!=VarValue::None());
  bool found=false;
  for(const auto& message:p.Messages()) {
    auto packet=dynamic_cast<CreateActorMessage*>(message.message.get());
    if(!packet || packet->baseId!=0x23ab8)continue;
    found=true;
    REQUIRE((packet->transform.pos==std::array<float,3>{1000,1000,100}));
    // Moving in the same execution turn cannot retract the already serialized packet.
    REQUIRE(packet->refrId.has_value());
    auto& created=p.worldState.GetFormAt<MpActor>(static_cast<uint32_t>(packet->refrId.value()));
    created.SetPos({9000,8000,250});
    REQUIRE((packet->transform.pos==std::array<float,3>{1000,1000,100}));
    break;
  }
  REQUIRE(found);
}
