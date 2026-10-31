#include "TestUtils.hpp"
#include "database_drivers/FileDatabase.h"
#include "save_storages/AsyncSaveStorage.h"
#include <filesystem>
#include <unistd.h>
PartOne& GetPartOne();
namespace {
constexpr uint32_t npcId = 0xff019001, base = 0x829b3;
void Configure(WorldState& w, uint32_t grace = 0) {
  w.ConfigureSrpNpcAdmission({{"schemaVersion",1},{"enabled",true},
    {"limits",{{"total",3},{"fauna",2},{"other",2},{"perRegion",2}}},
    {"sources",nlohmann::json::array()},{"faunaBases",{"829b3:Skyrim.esm"}}});
  w.ConfigureSrpNpcLiveUnload({{"schemaVersion",1},{"enabled",true},
    {"graceMs",uint32_t(grace)},{"maxNpcsPerHost",uint32_t(1)}});
  w.npcEnabled = false;
}
MpChangeForm Image(WorldState& w) {
  MpChangeForm a;
  a.recType=MpChangeForm::ACHR; a.profileId=-1;
  a.formDesc=FormDesc::FromFormId(npcId,w.espmFiles);
  a.baseDesc=FormDesc::FromFormId(base,w.espmFiles);
  a.worldOrCellDesc=FormDesc::Tamriel(); a.position={8192,0,128};
  a.actorValues.healthPercentage=0.37f;
  a.inv.entries.emplace_back(15,17);
  a.dynamicFields.SetValueDump("srpSpawnSource",R"("FAUNA")");
  a.dynamicFields.SetValueDump("srpMobInstanceId",R"("unload-existing-epoch")");
  return a;
}
void Storage(WorldState& w) {
  static int serial=0;
  auto dir=std::filesystem::temp_directory_path()/
    ("srp-live-unload-"+std::to_string(getpid())+"-"+std::to_string(++serial));
  std::filesystem::create_directories(dir);
  using S=Viet::AsyncSaveStorage<MpChangeForm,FormDesc,std::vector<FormDesc>>;
  w.AttachSaveStorage(std::make_shared<S>(std::make_shared<FileDatabase>(
    dir.string(),std::make_shared<spdlog::logger>("live-unload-test"),true)));
}
void UntilCold(WorldState& w) {
  for(int i=0;i<2200 && w.LookupFormByIdNoLoad(npcId);++i) {
    w.Tick(); std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  if (auto retained=w.LookupFormByIdNoLoad(npcId)) {
    auto* actor=retained->AsActor();
    INFO(MpChangeForm::ToJson(actor->GetChangeForm()).dump());
    INFO("pending=" << actor->HasSrpPendingNpcOperations() << " dependencies=" << actor->HasSrpUnloadDependencies());
    INFO(w.InspectSrpNpcAdmission().dump());
    REQUIRE_FALSE(w.LookupFormByIdNoLoad(npcId));
  }
}
}
TEST_CASE("R38.5 alive unload preserves the whole durable image and reloads one identity", "[srp-r385][espm]") {
  auto& w=GetPartOne().worldState; Configure(w); Storage(w);
  auto image=Image(w);
  SECTION("published fauna") {}
  SECTION("published native hunting") {image.dynamicFields.SetValueDump("srpSpawnSource",R"("HUNTING_NATIVE")");}
  w.LoadChangeForm(image,FormCallbacks::DoNothing());
  auto& a=w.GetFormAt<MpActor>(npcId);
  const auto before=MpChangeForm::ToJson(a.GetChangeForm());
  const std::weak_ptr<MpForm> ownership=w.LookupFormByIdNoLoad(npcId);
  const auto idx=a.GetIdx(); a.IncreaseBlockCount(); w.TickSrpNpcLiveUnload();
  REQUIRE(w.IsSrpNpcSuspending(npcId));
  REQUIRE(w.InspectSrpNpcAdmission().at("total")==1);
  UntilCold(w);
  REQUIRE(ownership.expired());
  REQUIRE(w.LookupFormByIdx(idx)==nullptr);
  REQUIRE(w.InspectSrpNpcAdmission().at("reservedNpcTombstones")==0);
  REQUIRE(w.InspectSrpNpcAdmission().at("total")==0);
  REQUIRE(w.InspectSrpNpcAdmission().at("liveUnloads")==1);
  auto& reloaded=w.GetFormAt<MpActor>(npcId);
  REQUIRE(MpChangeForm::ToJson(reloaded.GetChangeForm())==before);
  REQUIRE(&w.GetFormAt<MpActor>(npcId)==&reloaded);
  REQUIRE(w.GenerateFormId()!=npcId);
}
TEST_CASE("R38.5 missing persistence cannot release an alive NPC", "[srp-r385][espm]") {
  auto& w=GetPartOne().worldState; Configure(w);
  w.LoadChangeForm(Image(w),FormCallbacks::DoNothing());
  w.GetFormAt<MpActor>(npcId);
  for(int i=0;i<5;++i)w.Tick();
  REQUIRE(w.LookupFormByIdNoLoad(npcId));
  REQUIRE(w.InspectSrpNpcAdmission().at("total")==1);
  REQUIRE(w.IsSrpNpcSuspending(npcId));
}
TEST_CASE("R38.5 script timer VM reference pending callbacks and active effects retain actors", "[srp-r385][espm]") {
  auto& w=GetPartOne().worldState; Configure(w); Storage(w);
  w.LoadChangeForm(Image(w),FormCallbacks::DoNothing());
  auto& a=w.GetFormAt<MpActor>(npcId);
  SECTION("timer") { a.SetSingleUpdateTimerId(77); }
  SECTION("own trigger volume") { a.SetPrimitive({32,32,32}); }
  SECTION("VM references") { a.ToGameObject(); }
  SECTION("pending restoration") { a.UpdateNextRestorationTime(std::chrono::seconds(60)); }
  SECTION("active blocking") { a.SetIsBlockActive(true); }
  SECTION("callback") {
    struct Sink:MpActor::DestroyEventSink {void BeforeDestroy(MpActor&) override {FAIL("unload is not destroy");}};
    a.AddEventSink(std::make_shared<Sink>());
  }
  SECTION("pending activation dependency") {w.activationChildsByActivationParent[npcId][0xff000999]=1;}
  SECTION("active effect") {
    // Use the same valid vanilla regeneration effect as the resource suite.
    // A zero MGEF is rejected by ApplyMagicEffect before residency is tested.
    espm::Effects::Effect effect{0x3eb06,25,0,60};
    a.ApplyMagicEffect(effect);
    REQUIRE_FALSE(a.GetActiveMagicEffects().Empty());
  }
  for(int i=0;i<4;++i)w.Tick();
  REQUIRE(w.LookupFormByIdNoLoad(npcId));
  REQUIRE_FALSE(w.IsSrpNpcSuspending(npcId));
}
TEST_CASE("R38.5 corpse deletion unknown-owner and player identities cannot enter alive unload", "[srp-r385][espm]") {
  auto& w=GetPartOne().worldState; Configure(w);
  auto image=Image(w);
  SECTION("corpse") {image.isDead=true; image.actorValues.healthPercentage=0;}
  SECTION("deleted") {image.isDeleted=true; image.isDisabled=true;}
  SECTION("unknown owner") {image.dynamicFields.SetValueDump("srpSpawnSource",R"("UNKNOWN")");}
  SECTION("player") {image.profileId=991; image.baseDesc=FormDesc::FromString("7:Skyrim.esm");}
  w.LoadChangeForm(image,FormCallbacks::DoNothing());
  w.GetFormAt<MpActor>(npcId); w.TickSrpNpcLiveUnload();
  REQUIRE_FALSE(w.IsSrpNpcSuspending(npcId));
  REQUIRE(w.LookupFormByIdNoLoad(npcId));
}
TEST_CASE("R38.5 second observer retains NPC until both leave and host lease expires", "[srp-r385][espm]") {
  auto& p=GetPartOne(); auto& w=p.worldState; Configure(w); Storage(w);
  w.LoadChangeForm(Image(w),p.CreateFormCallbacks());
  auto& a=w.GetFormAt<MpActor>(npcId);
  for(uint32_t i=0;i<2;++i) {
    DoConnect(p,i); p.CreateActor(0xff001001+i,{8192,0,128},0,0x3c);
    p.SetUserActor(i,0xff001001+i);
  }
  w.GetFormAt<MpActor>(0xff001001).ForceSubscriptionsUpdate();
  w.GetFormAt<MpActor>(0xff001002).ForceSubscriptionsUpdate();
  REQUIRE(w.CanSrpNpcHost(a,w.GetFormAt<MpActor>(0xff001001)));
  DoMessage(p,0,{{"t",MsgType::Host},{"remoteId",npcId}});
  REQUIRE(w.hosters.at(npcId)==0xff001001);
  DoDisconnect(p,0); w.TickSrpNpcLiveUnload();
  REQUIRE_FALSE(w.hosters.count(npcId));
  REQUIRE_FALSE(w.IsSrpNpcSuspending(npcId));
  DoMessage(p,1,{{"t",MsgType::Host},{"remoteId",npcId}});
  REQUIRE(w.hosters.at(npcId)==0xff001002);
  DoDisconnect(p,1);
  UntilCold(w);
  REQUIRE(w.hosters.count(npcId)==0);
  DoConnect(p,1); p.SetUserActor(1,0xff001002);
  w.GetFormAt<MpActor>(0xff001002).ForceSubscriptionsUpdate();
  REQUIRE(w.LookupFormByIdNoLoad(npcId));
  REQUIRE(w.GetFormAt<MpActor>(npcId).GetInventory().GetItemCount(15)==17);
  DoDisconnect(p,1);
}
TEST_CASE("R38.5 interest returns during save and cancels unload without resurrecting", "[srp-r385][espm]") {
  auto& p=GetPartOne(); auto& w=p.worldState; Configure(w); Storage(w);
  w.LoadChangeForm(Image(w),p.CreateFormCallbacks()); w.GetFormAt<MpActor>(npcId);
  w.TickSrpNpcLiveUnload(); REQUIRE(w.IsSrpNpcSuspending(npcId));
  DoConnect(p,0); p.CreateActor(0xff001001,{8192,0,128},0,0x3c);p.SetUserActor(0,0xff001001);
  w.TickSrpNpcLiveUnload(); REQUIRE_FALSE(w.IsSrpNpcSuspending(npcId));
  REQUIRE(w.LookupFormByIdNoLoad(npcId));
  REQUIRE(w.InspectSrpNpcAdmission().at("cancelledUnloads")==1);
  DoDisconnect(p,0);
}
TEST_CASE("R38.5 host cap and remote-region host attempts do not load cold NPCs", "[srp-r385][espm]") {
  auto& p=GetPartOne(); auto& w=p.worldState; Configure(w);
  DoConnect(p,0); p.CreateActor(0xff001001,{8192,0,128},0,0x3c);p.SetUserActor(0,0xff001001);
  auto i=Image(w);w.LoadChangeForm(i,p.CreateFormCallbacks());
  auto& a=w.GetFormAt<MpActor>(npcId);w.GetFormAt<MpActor>(0xff001001).ForceSubscriptionsUpdate();
  auto second=Image(w);second.formDesc=FormDesc::FromFormId(npcId+1,w.espmFiles);second.position.x+=32;
  w.LoadChangeForm(second,p.CreateFormCallbacks());w.GetFormAt<MpActor>(npcId+1);
  w.hosters[npcId+1]=0xff001001;
  REQUIRE_FALSE(w.CanSrpNpcHost(a,w.GetFormAt<MpActor>(0xff001001)));
  w.hosters.clear(); REQUIRE(w.CanSrpNpcHost(a,w.GetFormAt<MpActor>(0xff001001)));
  auto far=Image(w);far.formDesc=FormDesc::FromFormId(npcId+10,w.espmFiles);far.position.x=409600;
  w.LoadChangeForm(far,p.CreateFormCallbacks());
  DoMessage(p,0,{{"t",MsgType::Host},{"remoteId",npcId+10}});
  REQUIRE_FALSE(w.LookupFormByIdNoLoad(npcId+10));
  DoDisconnect(p,0);
}
TEST_CASE("R38.5 unload policy rejects incompatible or late configuration", "[srp-r385][espm]") {
  auto& w=GetPartOne().worldState;
  REQUIRE_THROWS(w.ConfigureSrpNpcLiveUnload({{"schemaVersion",1},{"enabled",true},{"graceMs",uint32_t(0)},{"maxNpcsPerHost",uint32_t(1)}}));
  Configure(w);
  REQUIRE_THROWS(w.ConfigureSrpNpcLiveUnload({{"schemaVersion",1},{"enabled",true}}));
  w.LoadChangeForm(Image(w),FormCallbacks::DoNothing());w.GetFormAt<MpActor>(npcId);
  REQUIRE_THROWS_WITH(w.ConfigureSrpNpcLiveUnload({}),"SRP_NPC_UNLOAD_CONFIG_BEFORE_LOAD_REQUIRED");
}

TEST_CASE("R38.5 direct destroy cannot bypass an unload save barrier", "[srp-r385][espm]") {
  auto& w=GetPartOne().worldState; Configure(w);
  w.LoadChangeForm(Image(w),FormCallbacks::DoNothing());w.GetFormAt<MpActor>(npcId);
  w.TickSrpNpcLiveUnload();REQUIRE(w.IsSrpNpcSuspending(npcId));
  REQUIRE_THROWS_WITH(w.DestroyForm<MpActor>(npcId),"SRP_NPC_UNLOAD_REQUIRES_DURABLE_COMPLETION");
  REQUIRE(w.InspectSrpNpcAdmission().at("total")==1);
}

TEST_CASE("R38.5 inherited eligibility uses saved template selection and retains unknown chains", "[srp-r385][espm]") {
  auto& w=GetPartOne().worldState;Configure(w);Storage(w);
  auto i=Image(w); i.baseDesc=FormDesc::FromString("829b6:Skyrim.esm");
  SECTION("valid saved arctic fox chain") {
    i.templateChain={FormDesc::FromString("829b6:Skyrim.esm"),FormDesc::FromString("829b3:Skyrim.esm")};
    w.LoadChangeForm(i,FormCallbacks::DoNothing());auto& a=w.GetFormAt<MpActor>(npcId);
    const auto before=MpChangeForm::ToJson(a.GetChangeForm());UntilCold(w);
    REQUIRE(MpChangeForm::ToJson(w.GetFormAt<MpActor>(npcId).GetChangeForm())==before);
  }
  SECTION("incomplete chain is unknown") {
    i.templateChain={FormDesc::FromString("829b6:Skyrim.esm")};
    w.LoadChangeForm(i,FormCallbacks::DoNothing());auto& actor=w.GetFormAt<MpActor>(npcId);
    // ApplyChangeForm repairs an incomplete chain. Inject this corruption only
    // after that reader so this test exercises residency's UNKNOWN barrier.
    auto& damaged=const_cast<std::vector<FormDesc>&>(actor.GetTemplateChain());
    damaged=i.templateChain;
    w.TickSrpNpcLiveUnload();REQUIRE_FALSE(w.IsSrpNpcSuspending(npcId));
  }
}

TEST_CASE("R38.5 idx-only late packets cannot alias a reloaded NPC before connection drain", "[srp-r385][espm]") {
  auto& p=GetPartOne();auto& w=p.worldState;Configure(w);Storage(w);
  DoConnect(p,0);p.CreateActor(0xff001001,{409600,0,128},0,0x3c);p.SetUserActor(0,0xff001001);
  w.LoadChangeForm(Image(w),p.CreateFormCallbacks());
  const auto oldIdx=w.GetFormAt<MpActor>(npcId).GetIdx();
  UntilCold(w);REQUIRE(w.LookupFormByIdx(oldIdx)==nullptr);
  REQUIRE(w.InspectSrpNpcAdmission().at("total")==0);
  REQUIRE(w.InspectSrpNpcAdmission().at("quarantinedNetworkIndices")==1);
  const auto newIdx=w.GetFormAt<MpActor>(npcId).GetIdx();REQUIRE(newIdx!=oldIdx);
  auto late=jMovement;late["idx"]=oldIdx;
  DoMessage(p,0,late);
  REQUIRE(w.GetFormAt<MpActor>(npcId).GetPos().x==8192);
  REQUIRE_THROWS_WITH(w.Clear(),"SRP_NPC_LIVE_CLEAR_REQUIRES_NETWORK_DRAIN");
  // A new connection has no ownership of indices retired before it connected.
  DoConnect(p,1);DoDisconnect(p,0);
  REQUIRE(w.InspectSrpNpcAdmission().at("quarantinedNetworkIndices")==0);
  REQUIRE(w.LookupFormByIdx(oldIdx)==nullptr);
  DoDisconnect(p,1);
}

TEST_CASE("R38.5 reading a persisted corpse cannot initiate an implicit respawn", "[srp-r385][espm]") {
  auto& w=GetPartOne().worldState;Configure(w);Storage(w);
  auto i=Image(w);i.isDead=true;i.actorValues.healthPercentage=0;i.spawnDelay=0;
  w.LoadChangeForm(i,FormCallbacks::DoNothing());auto& a=w.GetFormAt<MpActor>(npcId);
  REQUIRE(a.IsDead());REQUIRE_FALSE(a.IsRespawning());
  for(int n=0;n<5;n++)w.Tick();REQUIRE(a.IsDead());REQUIRE_FALSE(a.IsRespawning());
  REQUIRE(a.GetChangeForm().actorValues.healthPercentage==0);
  REQUIRE_FALSE(w.IsSrpNpcSuspending(npcId));
}

TEST_CASE("R38.5 opt-in NPC host changes preserve non-actor object loading", "[srp-r385][espm]") {
  auto& p=GetPartOne();auto& w=p.worldState;Configure(w);
  DoConnect(p,0);p.CreateActor(0xff001001,{8192,0,128},0,0x3c);p.SetUserActor(0,0xff001001);
  constexpr uint32_t barrel=0x4cc2d; // Existing original barrel from activation tests.
  REQUIRE_FALSE(w.LookupFormByIdNoLoad(barrel));
  REQUIRE_NOTHROW(DoMessage(p,0,{{"t",MsgType::Host},{"remoteId",barrel}}));
  REQUIRE(w.LookupFormByIdNoLoad(barrel));
  REQUIRE(w.InspectSrpNpcAdmission().at("total")==0);
  REQUIRE(w.CanSrpNpcHost(*w.LookupFormByIdNoLoad(barrel)->AsObjectReference(),w.GetFormAt<MpActor>(0xff001001)));
  DoDisconnect(p,0);
}

TEST_CASE("R38.5 NPC region validation cannot reject legacy parked player images", "[srp-r385][espm]") {
  auto& w=GetPartOne().worldState;Configure(w);Storage(w);
  auto image=Image(w);image.baseDesc=FormDesc::FromString("7:Skyrim.esm");
  image.position={-1000000000.f,0,0};image.isDisabled=true;
  SECTION("profile-owned actor") {image.profileId=991;}
  SECTION("legacy reserved player base") {image.profileId=-1;}
  w.LoadChangeForm(image,FormCallbacks::DoNothing());
  auto& actor=w.GetFormAt<MpActor>(npcId);
  REQUIRE(actor.GetPos().x==image.position.x);REQUIRE(actor.IsDisabled());
  REQUIRE(actor.GetInventory().GetItemCount(15)==17);
  REQUIRE(w.InspectSrpNpcAdmission().at("total")==0);
  REQUIRE_FALSE(w.IsSrpNpcSuspending(npcId));
}
