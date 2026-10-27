#include "WorldState.h"
#include "Appearance.h"
#include "FormCallbacks.h"
#include "MpActor.h"
#include "libespm/Loader.h"
#include <catch2/catch_all.hpp>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <vector>

namespace {
// Test-only explicit-instantiation access: inspect allocations without changing
// production declarations, object layout, or adding a test setter for load state.
template <class Tag, typename Tag::type Member> struct InspectMember {
  friend typename Tag::type inspect(Tag) { return Member; }
};
using RefSet = std::unique_ptr<std::set<MpObjectReference*>>;
struct ListenersTag { using type = RefSet MpObjectReference::*; friend type inspect(ListenersTag); };
struct EmittersTag { using type = RefSet MpObjectReference::*; friend type inspect(EmittersTag); };
struct EverTag { using type = bool MpObjectReference::*; friend type inspect(EverTag); };
template struct InspectMember<ListenersTag, &MpObjectReference::listeners>;
template struct InspectMember<EmittersTag, &MpObjectReference::emitters>;
template struct InspectMember<EverTag, &MpObjectReference::everSubscribedOrListened>;

struct Events { size_t loadingSubscriptions = 0, normalSubscriptions = 0; };
FormCallbacks Observe(WorldState& world, Events& events)
{
  auto callbacks = FormCallbacks::DoNothing();
  callbacks.subscribe = [&](auto*, auto*) {
    if (world.IsFormLoadingInProgress()) ++events.loadingSubscriptions;
    else ++events.normalSubscriptions;
  };
  return callbacks;
}
struct ObservedRestoreReference final : MpObjectReference {
  WorldState& world;
  bool& seen;
  ObservedRestoreReference(WorldState& w, bool& s)
    : MpObjectReference(LocationalData(),FormCallbacks::DoNothing(),0x2000,"ACTI"), world(w), seen(s) {}
  void ApplyChangeForm(const MpChangeForm& c) override
  {
    REQUIRE(world.IsFormLoadingInProgress());
    seen=true;
    MpObjectReference::ApplyChangeForm(c);
  }
};
MpChangeForm ActorChange(uint32_t local = 0)
{
  MpChangeForm c;
  c.recType = MpChangeForm::ACHR;
  c.formDesc = {local, ""};
  c.baseDesc = {0xabcd, "Skyrim.esm"};
  c.worldOrCellDesc = FormDesc::Tamriel();
  c.position = {8192, 4096, 0};
  return c;
}
struct MiniPlugin {
  std::filesystem::path path;
  MiniPlugin()
  {
    auto word = [](char* p, uint32_t v) { std::memcpy(p, &v, 4); };
    auto record = [&](const char* type, uint32_t id, const std::vector<char>& payload) {
      std::vector<char> bytes(24 + payload.size(), '\0');
      std::memcpy(bytes.data(), type, 4); word(bytes.data()+4, static_cast<uint32_t>(payload.size())); word(bytes.data()+12,id);
      std::copy(payload.begin(),payload.end(),bytes.begin()+24); return bytes;
    };
    auto append=[](std::vector<char>& to,const std::vector<char>& from){to.insert(to.end(),from.begin(),from.end());};
    std::vector<char> payload(40,'\0');
    std::memcpy(payload.data(),"NAME",4); uint16_t size=4; std::memcpy(payload.data()+4,&size,2);word(payload.data()+6,0x2000);
    std::memcpy(payload.data()+10,"DATA",4);size=24;std::memcpy(payload.data()+14,&size,2);
    auto children=record("REFR",0x3000,payload);
    std::vector<char> group(24,'\0');std::memcpy(group.data(),"GRUP",4);word(group.data()+4,static_cast<uint32_t>(24+children.size()));word(group.data()+8,0x1000);word(group.data()+12,6);append(group,children);
    auto bytes=record("TES4",0,{});append(bytes,record("ACTI",0x2000,{}));append(bytes,record("CELL",0x1000,{}));append(bytes,group);
    path=std::filesystem::temp_directory_path()/("memory-guard-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".esm");
    std::ofstream out(path,std::ios::binary);out.write(bytes.data(),bytes.size());REQUIRE(out.good());
  }
  ~MiniPlugin(){std::error_code ec;std::filesystem::remove(path,ec);}
};
}

TEST_CASE("Memory guard A: restore registers grid without allocating subscriptions", "[MemoryGuard]")
{
  Events events; WorldState world; world.espmFiles={"Skyrim.esm"};
  world.LoadChangeForm(ActorChange(),Observe(world,events));
  auto& actor=world.GetFormAt<MpActor>(0xff000000);
  REQUIRE_FALSE(world.IsFormLoadingInProgress());
  REQUIRE((actor.*inspect(EverTag{})));
  REQUIRE_FALSE(actor.*inspect(ListenersTag{}));
  REQUIRE_FALSE(actor.*inspect(EmittersTag{}));
  REQUIRE(world.GetGrids().at(0x3c).grid->GetPos(&actor)==std::make_pair(int16_t(2),int16_t(1)));
  REQUIRE(events.loadingSubscriptions==0);
  REQUIRE(events.normalSubscriptions==0);
}

TEST_CASE("Memory guard B: explicit update outside restore creates subscriptions", "[MemoryGuard]")
{
  Events events; WorldState world; world.espmFiles={"Skyrim.esm"};
  world.LoadChangeForm(ActorChange(),Observe(world,events));
  auto& actor=world.GetFormAt<MpActor>(0xff000000);
  REQUIRE_FALSE(world.IsFormLoadingInProgress());
  actor.ForceSubscriptionsUpdate();
  REQUIRE(actor.*inspect(ListenersTag{}));
  REQUIRE(actor.*inspect(EmittersTag{}));
  REQUIRE(actor.GetListeners().count(&actor)==1);
  REQUIRE(actor.GetEmitters().count(&actor)==1);
  REQUIRE(events.loadingSubscriptions==0);
  REQUIRE(events.normalSubscriptions==1);
}

TEST_CASE("Memory guard C: restored actors transition to reciprocal subscriptions", "[MemoryGuard]")
{
  Events events; WorldState world; world.espmFiles={"Skyrim.esm"};
  auto callbacks=Observe(world,events);
  world.LoadChangeForm(ActorChange(0),callbacks);world.LoadChangeForm(ActorChange(1),callbacks);
  auto& a=world.GetFormAt<MpActor>(0xff000000);auto& b=world.GetFormAt<MpActor>(0xff000001);
  REQUIRE_FALSE(a.*inspect(ListenersTag{}));REQUIRE_FALSE(b.*inspect(ListenersTag{}));
  a.ForceSubscriptionsUpdate();b.ForceSubscriptionsUpdate();
  REQUIRE((a.GetListeners()==std::set<MpObjectReference*>{&a,&b}));
  REQUIRE((b.GetListeners()==std::set<MpObjectReference*>{&a,&b}));
  REQUIRE((a.GetEmitters()==std::set<MpObjectReference*>{&a,&b}));
  REQUIRE((b.GetEmitters()==std::set<MpObjectReference*>{&a,&b}));
  REQUIRE(events.loadingSubscriptions==0);REQUIRE(events.normalSubscriptions==4);
}

TEST_CASE("Memory guard load flag resets on existing returns and exception exits", "[MemoryGuard]")
{
  MiniPlugin file;espm::Loader loader(std::vector<std::filesystem::path>{file.path});
  WorldState world;world.AttachEspm(&loader,[]{return FormCallbacks::DoNothing();});
  MpChangeForm c;c.recType=MpChangeForm::REFR;c.baseDesc={0x2000,file.path.filename().string()};c.worldOrCellDesc={0x1000,file.path.filename().string()};
  REQUIRE_FALSE(world.IsFormLoadingInProgress());
  SECTION("missing base record returns"){c.baseDesc.shortFormId=0xeeee;world.LoadChangeForm(c,FormCallbacks::DoNothing());}
  SECTION("malformed appearance JSON returns"){c.appearanceDump="{";world.LoadChangeForm(c,FormCallbacks::DoNothing());}
  SECTION("invalid appearance structure returns"){c.appearanceDump="{}";world.LoadChangeForm(c,FormCallbacks::DoNothing());}
  SECTION("invalid race returns"){Appearance a;a.raceId=0xdead;c.appearanceDump=a.ToJson();world.LoadChangeForm(c,FormCallbacks::DoNothing());}
  SECTION("deferred static form returns"){c.formDesc={0x4000,file.path.filename().string()};world.LoadChangeForm(c,FormCallbacks::DoNothing());}
  SECTION("existing form apply returns"){
    bool seen=false;
    auto ref=std::make_unique<ObservedRestoreReference>(world,seen);
    world.AddForm(std::move(ref),0x3000);c.formDesc={0x3000,file.path.filename().string()};world.LoadChangeForm(c,FormCallbacks::DoNothing());
    REQUIRE(seen);
  }
  SECTION("normal dynamic form returns"){world.LoadChangeForm(c,FormCallbacks::DoNothing());}
  SECTION("unknown record type throws"){c.recType=static_cast<decltype(c.recType)>(99);REQUIRE_THROWS(world.LoadChangeForm(c,FormCallbacks::DoNothing()));}
  SECTION("unknown plugin throws"){c.baseDesc.file="absent.esm";REQUIRE_THROWS(world.LoadChangeForm(c,FormCallbacks::DoNothing()));}
  REQUIRE_FALSE(world.IsFormLoadingInProgress());
}

TEST_CASE("Memory guard prevents plugin neighborhood initialization until restore ends", "[MemoryGuard]")
{
  MiniPlugin file;espm::Loader loader(std::vector<std::filesystem::path>{file.path});
  WorldState world;world.AttachEspm(&loader,[]{return FormCallbacks::DoNothing();});
  MpChangeForm c;c.recType=MpChangeForm::REFR;c.baseDesc={0x2000,file.path.filename().string()};c.worldOrCellDesc={0x1000,file.path.filename().string()};c.position={4096,4096,0};
  world.LoadChangeForm(c,FormCallbacks::DoNothing());
  auto& ref=world.GetFormAt<MpObjectReference>(0xff000000);
  REQUIRE_FALSE(ref.*inspect(ListenersTag{}));
  REQUIRE(world.GetGrids().at(0x1000).loadedChunks.empty());
  REQUIRE_FALSE(world.LookupFormByIdNoLoad(0x3000));
  ref.ForceSubscriptionsUpdate();
  REQUIRE_FALSE(world.GetGrids().at(0x1000).loadedChunks.empty());
  REQUIRE(world.LookupFormByIdNoLoad(0x3000));
}
