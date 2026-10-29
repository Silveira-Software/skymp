#include "SrpActorModifiers.h"
#include "SrpActorStats.h"
#include "TestUtils.hpp"
#include <catch2/catch_approx.hpp>
#include "database_drivers/FileDatabase.h"
#include "save_storages/AsyncSaveStorage.h"
#include <filesystem>
#include <fstream>
#include <csignal>
#include <sys/wait.h>
#include <poll.h>
#include <unistd.h>
PartOne& GetPartOne();
namespace {
MpActor& Create(PartOne& w){w.CreateActor(0xff000000,{0,0,0},0,0x3c,1);auto& a=w.worldState.GetFormAt<MpActor>(0xff000000);nlohmann::json p={{"schemaVersion",2},{"rulesVersion","SRP-PROGRESSION-A1"},{"revision",1},{"level",4},{"profileId",std::to_string(a.GetProfileId())},{"characterId","r37"},{"attributes",{{"vigor",10},{"strength",10},{"dexterity",10},{"intelligence",10},{"willpower",10},{"constitution",10}}}};a.SetPropertyValueDump("srpActiveCharacterId","\"r37\"",false,false);a.SetPropertyValueDump("srpProgression",p.dump(),false,false);a.ApplySrpActorStats(p);return a;}
float Health(MpActor& a){return a.GetActorValues().healthPercentage*a.GetMaximumValues().health;}
}
TEST_CASE("R37 fortify is a reversible maximum and never healing", "[srp-r37]"){
 auto& w=GetPartOne();auto& a=Create(w);a.DamageActorValue(espm::ActorValue::Health,20);const auto hp=Health(a);
 espm::Effects::Effect e{0x3eaf3,50,0,60};a.ApplyMagicEffect(e);REQUIRE(a.GetMaximumValues().health==Catch::Approx(174));REQUIRE(Health(a)==Catch::Approx(hp));
 a.ApplyMagicEffect(e);REQUIRE(a.GetMaximumValues().health==Catch::Approx(174));REQUIRE(Health(a)==Catch::Approx(hp));
 a.RemoveMagicEffect(espm::ActorValue::Health);REQUIRE(a.GetMaximumValues().health==Catch::Approx(124));REQUIRE(Health(a)==Catch::Approx(hp));
}
TEST_CASE("R37 restore health still restores while fortify coexists", "[srp-r37]"){
 auto& w=GetPartOne();auto& a=Create(w);a.DamageActorValue(espm::ActorValue::Health,40);const auto hp=Health(a);espm::Effects::Effect fortify{0x3eaf3,50,0,60},heal{0x3eb15,10,0,0};a.ApplyMagicEffect(fortify);a.ApplyMagicEffect(heal);REQUIRE(Health(a)==Catch::Approx(hp+10));REQUIRE(a.GetMaximumValues().health==Catch::Approx(174));a.RemoveAllMagicEffects();REQUIRE(a.GetMaximumValues().health==Catch::Approx(124));REQUIRE(Health(a)==Catch::Approx(hp+10));
}
TEST_CASE("R37 fortify three resources survives durable changeform decode", "[srp-r37]"){
 auto& w=GetPartOne();auto& a=Create(w);for(auto id:{0x3eaf3,0x3eaf8,0x3eaf9}){espm::Effects::Effect e{static_cast<uint32_t>(id),30,0,60};a.ApplyMagicEffect(e);}const auto saved=a.GetChangeForm();const auto encoded=MpChangeForm::ToJson(saved).dump();simdjson::dom::parser parser;auto d=parser.parse(encoded);auto element=d.value();auto decoded=MpChangeForm::JsonToChangeForm(element);CAPTURE(encoded);REQUIRE(decoded.activeMagicEffects.Has(espm::ActorValue::Health));auto& w2=GetPartOne();auto& b=Create(w2);b.ApplyChangeForm(decoded);REQUIRE(b.GetMaximumValues().health==Catch::Approx(154));REQUIRE(b.GetMaximumValues().magicka==Catch::Approx(154));REQUIRE(b.GetMaximumValues().stamina==Catch::Approx(154));REQUIRE(Health(b)==Catch::Approx(saved.actorValues.healthPercentage*saved.actorValues.health));b.RemoveAllMagicEffects();REQUIRE(b.GetMaximumValues().health==Catch::Approx(124));
}
TEST_CASE("R37 modifier composition is bounded and does not change regeneration", "[srp-r37]"){
 SrpActorModifiers m;m.equipment[espm::ActorValue::Health]=20;m.temporary[espm::ActorValue::Health]=50;m.passive[espm::ActorValue::Health]=-10;auto x=m.Compose(BaseActorValues{});REQUIRE(x.health==Catch::Approx(160));REQUIRE(x.healRate==Catch::Approx(.7));m.temporary[espm::ActorValue::Health]=1000000;REQUIRE(m.Compose(BaseActorValues{}).health<=10100);m.temporary[espm::ActorValue::Health]=-1000000;REQUIRE(m.Compose(BaseActorValues{}).health==1);
}
TEST_CASE("R37 enchanted armor uses inventory identity and removes only its own maximum", "[srp-r37]"){
 auto& w=GetPartOne();auto& a=Create(w);Inventory inv;Inventory::Entry armor(0x12e49,1);armor.enchantmentId=0x49509;armor.name="R37 verified variant";inv.entries.push_back(armor);a.SetInventory(inv);Equipment worn;armor.SetWorn(Inventory::Worn::Right);worn.inv.entries.push_back(armor);const auto hp=Health(a);a.SetEquipment(worn);REQUIRE(a.GetMaximumValues().health==Catch::Approx(144));REQUIRE(Health(a)==Catch::Approx(hp));a.SetEquipment(worn);REQUIRE(a.GetMaximumValues().health==Catch::Approx(144));
 espm::Effects::Effect potion{0x3eaf3,30,0,60};a.ApplyMagicEffect(potion);REQUIRE(a.GetMaximumValues().health==Catch::Approx(174));a.SetEquipment(Equipment{});REQUIRE(a.GetMaximumValues().health==Catch::Approx(154));REQUIRE(Health(a)==Catch::Approx(hp));
 worn.inv.entries.front().enchantmentId=0xbdffe;a.SetEquipment(worn);REQUIRE(a.GetMaximumValues().health==Catch::Approx(154));REQUIRE(a.GetInventory().entries.front().enchantmentId==0x49509);a.RemoveAllMagicEffects();REQUIRE(a.GetMaximumValues().health==Catch::Approx(124));
}
TEST_CASE("R37 expired durable modifier does not revive or heal after decode", "[srp-r37]"){
 auto& w=GetPartOne();auto& a=Create(w);a.DamageActorValue(espm::ActorValue::Health,15);espm::Effects::Effect e{0x3eaf3,30,0,0};a.ApplyMagicEffect(e);const auto encoded=MpChangeForm::ToJson(a.GetChangeForm()).dump();simdjson::dom::parser parser;auto d=parser.parse(encoded);auto element=d.value();auto decoded=MpChangeForm::JsonToChangeForm(element);auto& w2=GetPartOne();auto& b=Create(w2);b.ApplyChangeForm(decoded);REQUIRE(b.GetMaximumValues().health==Catch::Approx(124));REQUIRE(Health(b)==Catch::Approx(85));REQUIRE_FALSE(b.GetActiveMagicEffects().Has(espm::ActorValue::Health));
}
TEST_CASE("R37 every inventory writer reconciles removed worn enchantment without healing", "[srp-r37]"){
 for(int writer=0;writer<3;writer++){auto& w=GetPartOne();auto& a=Create(w);Inventory inv;Inventory::Entry armor(0x12e49,1);armor.enchantmentId=0x49509;inv.entries.push_back(armor);a.SetInventory(inv);Equipment worn;armor.SetWorn(Inventory::Worn::Right);worn.inv.entries.push_back(armor);a.SetEquipment(worn);a.DamageActorValue(espm::ActorValue::Health,20);const auto hp=Health(a);if(writer==0)a.SetInventory(Inventory{});else if(writer==1)a.RemoveItems(inv.entries,nullptr);else a.RemoveAllItems(nullptr);REQUIRE(a.GetMaximumValues().health==Catch::Approx(124));REQUIRE(Health(a)==Catch::Approx(hp));REQUIRE(a.GetActorValues().health==Catch::Approx(124));}
}
TEST_CASE("R37 world timer expires fortify without losing health", "[srp-r37]"){
 auto& w=GetPartOne();auto& a=Create(w);a.DamageActorValue(espm::ActorValue::Health,25);const auto hp=Health(a);espm::Effects::Effect e{0x3eaf3,50,0,1};a.ApplyMagicEffect(e);REQUIRE(a.GetMaximumValues().health==Catch::Approx(174));std::this_thread::sleep_for(std::chrono::milliseconds(1100));w.Tick();REQUIRE(a.GetMaximumValues().health==Catch::Approx(124));REQUIRE(Health(a)==Catch::Approx(hp));
}
TEST_CASE("R37 reads legacy R36 resource percentages and rejects corrupt maximum metadata", "[srp-r37]"){
 auto& w=GetPartOne();auto& a=Create(w);a.DamageActorValue(espm::ActorValue::Health,20);const auto hp=Health(a);auto value=MpChangeForm::ToJson(a.GetChangeForm());value["dynamicFields"].erase("private.srpResourceMaximumsV1");auto decode=[](const auto& json){simdjson::dom::parser parser;auto d=parser.parse(json.dump());auto element=d.value();return MpChangeForm::JsonToChangeForm(element);};auto legacy=decode(value);a.ApplyChangeForm(legacy);REQUIRE(Health(a)==Catch::Approx(hp));value["dynamicFields"]["private.srpResourceMaximumsV1"]={{"schemaVersion",1},{"health",-1},{"magicka",124},{"stamina",124}};auto corrupt=decode(value);REQUIRE_THROWS(a.ApplyChangeForm(corrupt));
}
TEST_CASE("R37 distinct temporary effects on one resource survive replay reload and independent expiration", "[srp-r37]"){
 auto& w=GetPartOne();auto& a=Create(w);a.DamageActorValue(espm::ActorValue::Health,25);const auto hp=Health(a);espm::Effects::Effect potion{0x3eaf3,50,0,1},other{0x493aa,20,0,60};a.ApplyMagicEffect(potion);a.ApplyMagicEffect(other);REQUIRE(a.GetMaximumValues().health==Catch::Approx(194));a.ApplyMagicEffect(other);REQUIRE(a.GetMaximumValues().health==Catch::Approx(194));REQUIRE(Health(a)==Catch::Approx(hp));const auto value=MpChangeForm::ToJson(a.GetChangeForm()).dump();simdjson::dom::parser parser;auto d=parser.parse(value);auto element=d.value();auto decoded=MpChangeForm::JsonToChangeForm(element);REQUIRE(decoded.activeMagicEffects.GetAllEffects().size()==2);std::this_thread::sleep_for(std::chrono::milliseconds(1100));w.Tick();REQUIRE(a.GetMaximumValues().health==Catch::Approx(144));REQUIRE(Health(a)==Catch::Approx(hp));REQUIRE(a.GetActiveMagicEffects().Has(espm::ActorValue::Health));a.RemoveAllMagicEffects();REQUIRE(a.GetMaximumValues().health==Catch::Approx(124));REQUIRE(Health(a)==Catch::Approx(hp));
}
TEST_CASE("R37 rank changes preserve legacy regeneration and restore its remaining timer", "[srp-r37]"){
 auto& w=GetPartOne();auto& a=Create(w);espm::Effects::Effect regen{0x3eb06,25,0,60};a.ApplyMagicEffect(regen);const auto rate=a.GetActorValues().healRateMult;REQUIRE(rate>100);const auto before=Health(a);Equipment empty;a.SetEquipment(empty);REQUIRE(a.GetActorValues().healRateMult==rate);const auto value=MpChangeForm::ToJson(a.GetChangeForm()).dump();simdjson::dom::parser parser;auto d=parser.parse(value);auto element=d.value();auto decoded=MpChangeForm::JsonToChangeForm(element);a.ApplyChangeForm(decoded);REQUIRE(a.GetActorValues().healRateMult==rate);REQUIRE(Health(a)==Catch::Approx(before));REQUIRE(a.GetMaximumValues().health==Catch::Approx(124));
}

TEST_CASE("R37 corrupt or absent magic effect cannot mutate resources", "[srp-r37]"){
 auto& w=GetPartOne();auto& a=Create(w);const auto hp=Health(a),maximum=a.GetMaximumValues().health;espm::Effects::Effect invalid{0,50,0,60};REQUIRE_THROWS(a.ApplyMagicEffect(invalid));REQUIRE(a.GetMaximumValues().health==maximum);REQUIRE(Health(a)==Catch::Approx(hp));REQUIRE(a.GetActiveMagicEffects().Empty());
}

TEST_CASE("R37 conditional vanilla MGEF cannot be assumed active", "[srp-r37]"){
 auto& w=GetPartOne();auto& a=Create(w);const auto before=a.GetMaximumValues();espm::Effects::Effect conditional{0xf81e5,40,0,60};REQUIRE_THROWS_WITH(a.ApplyMagicEffect(conditional),"SRP_MODIFIER_CONDITION_REVIEW_REQUIRED");REQUIRE(a.GetMaximumValues().magicka==before.magicka);REQUIRE(a.GetActiveMagicEffects().Empty());
}

TEST_CASE("R37 CTDA subject comparisons use canonical inventory and race", "[srp-r37]"){
 auto& w=GetPartOne();auto& a=Create(w);
 const auto owner=w.worldState.GetEspm().GetBrowser().LookupById(0x3eaf3);
 using R=SrpActorModifiers::ConditionResult;
 auto run=[&](const std::vector<espm::CTDA>& c){return SrpActorModifiers::EvaluateConditions(a,owner,c);};
 espm::CTDA race{};race.functionIndex=69;race.runOnType=espm::CTDA::RunOnTypeFlags::Subject;race.comparisonValue=1;auto raceId=a.GetRaceId();memcpy(race.functionData,&raceId,4);
 REQUIRE(run({})==R::Active);REQUIRE(run({race})==R::Active);
 race.comparisonValue=0;REQUIRE(run({race})==R::Inactive);
 espm::CTDA count{};count.functionIndex=47;count.runOnType=espm::CTDA::RunOnTypeFlags::Subject;uint32_t item=0xf;memcpy(count.functionData,&item,4);
 Inventory inv;inv.AddItem(item,2);a.SetInventory(inv);
 for(auto [op,value,expected]:std::vector<std::tuple<uint8_t,float,bool>>{{0,2,true},{1,2,false},{2,1,true},{3,2,true},{4,3,true},{5,2,true}}){count.operatorFlag=op<<5;count.comparisonValue=value;REQUIRE((run({count})==R::Active)==expected);}
 count.operatorFlag=0;count.comparisonValue=2;REQUIRE(run({count})==R::Active);a.SetInventory(Inventory{});REQUIRE(run({count})==R::Inactive);
}

TEST_CASE("R37 CTDA preserves AND OR grouping and fails closed on unsupported context", "[srp-r37]"){
 auto& w=GetPartOne();auto& a=Create(w);const auto owner=w.worldState.GetEspm().GetBrowser().LookupById(0x3eaf3);using R=SrpActorModifiers::ConditionResult;
 auto run=[&](const std::vector<espm::CTDA>& c){return SrpActorModifiers::EvaluateConditions(a,owner,c);};
 espm::CTDA yes{};yes.functionIndex=300;yes.runOnType=espm::CTDA::RunOnTypeFlags::Subject;yes.comparisonValue=0;
 auto no=yes;no.comparisonValue=1;auto orNo=no;orNo.operatorFlag=1;
 REQUIRE(run({yes})==R::Active);REQUIRE(run({no})==R::Inactive);
 REQUIRE(run({yes,no})==R::Inactive);REQUIRE(run({orNo,yes})==R::Active);
 REQUIRE(run({orNo,yes,no})==R::Inactive);
 for(uint8_t flag:{2,4,8,16}){auto c=yes;c.operatorFlag=flag;REQUIRE(run({c})==R::Unknown);}
 auto c=yes;c.runOnType=espm::CTDA::RunOnTypeFlags::Target;REQUIRE(run({c})==R::Unknown);
 c=yes;c.reference=0x14;REQUIRE(run({c})==R::Unknown);
 c=yes;c.functionIndex=715;REQUIRE(run({c})==R::Unknown);
 REQUIRE(run({no,c})==R::Unknown);REQUIRE(run({orNo,c})==R::Unknown);
 c=yes;c.operatorFlag=7<<5;REQUIRE(run({c})==R::Unknown);
 c=yes;c.comparisonValue=std::numeric_limits<float>::quiet_NaN();REQUIRE(run({c})==R::Unknown);
 c=yes;c.functionIndex=69;uint32_t invalid=0x00ffffff;memcpy(c.functionData,&invalid,4);REQUIRE(run({c})==R::Unknown);
}

namespace {
espm::Effects::Effect SourceSpell(PartOne& w,uint32_t id,uint32_t index){const auto owner=w.worldState.GetEspm().GetBrowser().LookupById(id);const auto e=espm::GetData<espm::SPEL>(id,&w.worldState).effects.at(index);return {owner.ToGlobalId(e.effectFormId),e.effectItem->magnitude,e.effectItem->areaOfEffect,e.effectItem->duration};}
ActiveMagicEffectsMap DecodeEffects(const nlohmann::json& value){simdjson::dom::parser p;auto d=p.parse(value.dump());auto e=d.value();return ActiveMagicEffectsMap::FromJson(e);}
}
TEST_CASE("R37 provenance binds actual source EFID caster target and survives durable reload", "[srp-r37]"){
 auto& w=GetPartOne();auto& a=Create(w);a.DamageActorValue(espm::ActorValue::Health,20);const auto hp=Health(a);
 Inventory inv;inv.AddItem(0x3eaf2,2);a.SetInventory(inv);REQUIRE(a.OnEquip(0x3eaf2));
 const auto entries=a.GetActiveMagicEffects().GetEntries();REQUIRE(entries.size()==1);REQUIRE(entries[0].provenance.has_value());const auto p=*entries[0].provenance;
 REQUIRE(p.at("source")=="3eaf2:Skyrim.esm");REQUIRE(p.at("mgef")=="3eaf3:Skyrim.esm");REQUIRE(p.at("sourceType")=="ALCH");REQUIRE(p.at("effectIndex")==0);REQUIRE(p.at("target")==p.at("caster"));REQUIRE_FALSE(p.at("instanceId").get<std::string>().empty());REQUIRE(Health(a)==Catch::Approx(hp));
 auto decoded=DecodeEffects(a.GetActiveMagicEffects().ToJson());REQUIRE(decoded.GetEntries().size()==1);REQUIRE(decoded.GetEntries()[0].provenance==entries[0].provenance);
 const auto saved=a.GetChangeForm();a.ApplyChangeForm(saved);REQUIRE(Health(a)==Catch::Approx(hp));REQUIRE(a.GetActiveMagicEffects().GetEntries()[0].provenance==entries[0].provenance);
}
TEST_CASE("R37 two casters coexist same source and recast replaces only its own instance", "[srp-r37]"){
 auto& w=GetPartOne();auto& a=Create(w);w.CreateActor(0xff000001,{0,0,0},0,0x3c,2);auto& b=w.worldState.GetFormAt<MpActor>(0xff000001);b.SetPropertyValueDump("srpActiveCharacterId","\"caster-b\"",false,false);
 auto e=SourceSpell(w,0xfb994,0);a.DamageActorValue(espm::ActorValue::Health,20);const auto hp=Health(a);
 auto one=SrpActorModifiers::CreateProvenance(a,0xfb994,0,a,"cast-a");a.ApplyMagicEffect(e,false,false,&one);
 auto two=SrpActorModifiers::CreateProvenance(a,0xfb994,0,b,"cast-b");a.ApplyMagicEffect(e,false,false,&two);
 REQUIRE(a.GetMaximumValues().health==Catch::Approx(149));REQUIRE(a.GetActiveMagicEffects().GetEntries().size()==2);REQUIRE(Health(a)==Catch::Approx(hp));
 auto retry=SrpActorModifiers::CreateProvenance(a,0xfb994,0,a,"cast-a");a.ApplyMagicEffect(e,false,false,&retry);REQUIRE(a.GetActiveMagicEffects().GetByStackKey(espm::ActorValue::Health,one.at("stackKey").get<std::string>())->get().provenance->at("instanceId")==one.at("instanceId"));
 auto recast=SrpActorModifiers::CreateProvenance(a,0xfb994,0,a,"cast-a-next");a.ApplyMagicEffect(e,false,false,&recast);REQUIRE(a.GetActiveMagicEffects().GetEntries().size()==2);REQUIRE(a.GetMaximumValues().health==Catch::Approx(149));REQUIRE(Health(a)==Catch::Approx(hp));
 auto saved=a.GetChangeForm();a.ApplyChangeForm(saved);REQUIRE(a.GetActiveMagicEffects().GetEntries().size()==2);REQUIRE(a.GetMaximumValues().health==Catch::Approx(149));
}
TEST_CASE("R37 source provenance rejects corrupt source index target and payload before mutation", "[srp-r37]"){
 auto& w=GetPartOne();auto& a=Create(w);auto e=SourceSpell(w,0xfb994,0);const auto valid=SrpActorModifiers::CreateProvenance(a,0xfb994,0,a,"cast");
 const auto before=a.GetActorValues();
 for(const auto& key:{"source","mgef","effectIndex","target","stackKey"}){auto p=valid;if(std::string(key)=="effectIndex")p[key]=uint32_t(4000);else if(std::string(key)=="target")p[key]["characterId"]="foreign";else p[key]="invalid";REQUIRE_THROWS(a.ApplyMagicEffect(e,false,false,&p));REQUIRE(a.GetActiveMagicEffects().Empty());REQUIRE(a.GetActorValues().healthPercentage==before.healthPercentage);}
 auto bad=e;bad.magnitude+=1;REQUIRE_THROWS(a.ApplyMagicEffect(bad,false,false,&valid));REQUIRE(a.GetActiveMagicEffects().Empty());
}
TEST_CASE("R37 legacy rows remain unknown and corrupted or duplicate provenance never silently migrates", "[srp-r37]"){
 auto& w=GetPartOne();auto& a=Create(w);espm::Effects::Effect old{0x3eaf3,20,0,60};a.ApplyMagicEffect(old);auto legacy=a.GetActiveMagicEffects().ToJson();REQUIRE_FALSE(legacy[0].contains("srpProvenanceV1"));auto d=DecodeEffects(legacy);REQUIRE_FALSE(d.GetEntries()[0].provenance.has_value());REQUIRE_FALSE(SrpActorModifiers::Read(a).complete);
 a.RemoveAllMagicEffects();auto e=SourceSpell(w,0xfb994,0);auto p=SrpActorModifiers::CreateProvenance(a,0xfb994,0,a,"new");a.ApplyMagicEffect(e,false,false,&p);auto json=a.GetActiveMagicEffects().ToJson();auto duplicate=json;duplicate.push_back(json[0]);REQUIRE_THROWS(DecodeEffects(duplicate));json[0]["srpProvenanceV1"]["schemaVersion"]=999;REQUIRE_THROWS(DecodeEffects(json));
}
TEST_CASE("R37 equipment CTDA trusts actual variants and not forged worn metadata", "[srp-r37]"){
 auto& w=GetPartOne();auto& a=Create(w);auto owner=w.worldState.GetEspm().GetBrowser().LookupById(0x3eaf3);espm::CTDA c{};c.runOnType=espm::CTDA::RunOnTypeFlags::Subject;c.functionIndex=182;c.comparisonValue=1;uint32_t id=0x12e49;memcpy(c.functionData,&id,4);using R=SrpActorModifiers::ConditionResult;
 auto evaluate=[&](){return SrpActorModifiers::EvaluateConditions(a,owner,{c});};Inventory inv;Inventory::Entry entry(id,1);entry.name="actual";inv.entries.push_back(entry);a.SetInventory(inv);Equipment eq;entry.SetWorn(Inventory::Worn::Right);eq.inv.entries.push_back(entry);a.SetEquipment(eq);REQUIRE(evaluate()==R::Active);eq.inv.entries[0].name="forged";a.SetEquipment(eq);REQUIRE(evaluate()==R::Inactive);
 c.functionIndex=58;REQUIRE(evaluate()==R::Unknown);c.functionIndex=74;REQUIRE(evaluate()==R::Unknown);c.functionIndex=448;REQUIRE(evaluate()==R::Unknown);
}

TEST_CASE("R37 source metadata records enchantment and rate policy without changing legacy regeneration", "[srp-r37]"){
 auto& w=GetPartOne();auto& a=Create(w);auto ench=SrpActorModifiers::CreateProvenance(a,0x49509,0,a,"enchantment-context");REQUIRE(ench.at("sourceType")=="ENCH");REQUIRE(ench.at("source")=="49509:Skyrim.esm");
 Inventory inv;inv.AddItem(0x3eb09,1);a.SetInventory(inv);
 // Regeneration direct legacy remains supported and never invents provenance.
 espm::Effects::Effect regen{0x3eb06,25,0,60};a.ApplyMagicEffect(regen);REQUIRE(a.GetActorValues().healRateMult>100);REQUIRE_FALSE(a.GetActiveMagicEffects().GetEntries()[0].provenance.has_value());
}
TEST_CASE("R37 changeform transaction failure restores effect origin and current resources", "[srp-r37]"){
 auto& w=GetPartOne();auto& a=Create(w);a.DamageActorValue(espm::ActorValue::Health,20);const auto before=MpChangeForm::ToJson(a.GetChangeForm());
 REQUIRE_THROWS(a.RunChangeFormTransaction([&](){SrpActorModifiers::ApplyFromSource(a,0x3eaf2,a,"failure-before-save");throw std::runtime_error("FORCED_WRITE_ABORT");return true;}));
 REQUIRE(MpChangeForm::ToJson(a.GetChangeForm())==before);REQUIRE(a.GetActiveMagicEffects().Empty());REQUIRE(a.GetMaximumValues().health==Catch::Approx(124));
}
TEST_CASE("R37 origin expiry tampering and condition-context changes are rejected on reload", "[srp-r37]"){
 auto& w=GetPartOne();auto& a=Create(w);SrpActorModifiers::ApplyFromSource(a,0x3eaf2,a,"expire-test");auto json=a.GetActiveMagicEffects().ToJson();auto corrupt=json;corrupt[0]["srpProvenanceV1"]["appliedAtMs"]=0;REQUIRE_THROWS(DecodeEffects(corrupt));
 auto saved=a.GetChangeForm();auto rows=saved.activeMagicEffects.ToJson();rows[0]["srpProvenanceV1"]["mgefConditionFingerprint"]="changed";saved.activeMagicEffects=DecodeEffects(rows);REQUIRE_THROWS(a.ApplyChangeForm(saved));
}

TEST_CASE("R37 epoch expiration remains valid independently of the legacy locale timestamp", "[srp-r37]"){
 auto& w=GetPartOne();auto& a=Create(w);SrpActorModifiers::ApplyFromSource(a,0x3eaf2,a,"epoch");auto rows=a.GetActiveMagicEffects().ToJson();rows[0]["endTime"]="legacy display from another locale";auto read=DecodeEffects(rows);REQUIRE(read.GetEntries().size()==1);REQUIRE(read.GetEntries()[0].endTime==a.GetActiveMagicEffects().GetEntries()[0].endTime);REQUIRE(read.GetEntries()[0].provenance==a.GetActiveMagicEffects().GetEntries()[0].provenance);
}
TEST_CASE("R37 source metadata is not applied to a replacement character on the same actor", "[srp-r37]"){
 auto& w=GetPartOne();auto& a=Create(w);SrpActorModifiers::ApplyFromSource(a,0x3eaf2,a,"owner");const auto stored=a.GetActiveMagicEffects().ToJson();a.SetPropertyValueDump("srpActiveCharacterId","\"replacement\"",false,false);auto modifiers=SrpActorModifiers::Read(a);REQUIRE_FALSE(modifiers.complete);REQUIRE(modifiers.Sum(espm::ActorValue::Health)==0);REQUIRE(a.GetActiveMagicEffects().ToJson()==stored);
}

TEST_CASE("R37 origin inventory and maxima remain one durable world record across SIGKILL", "[srp-r37][crash]"){
 namespace fs=std::filesystem;
 for(int stage=0;stage<3;++stage){
  const auto path=fs::temp_directory_path()/("srp-r37-origin-crash-"+std::to_string(getpid())+"-"+std::to_string(stage));fs::create_directories(path);int signal[2];REQUIRE(pipe(signal)==0);const auto pid=fork();REQUIRE(pid>=0);
  if(pid==0){
   close(signal[0]);try{
    auto& w=GetPartOne();auto& a=Create(w);
    using Storage=Viet::AsyncSaveStorage<MpChangeForm,FormDesc,std::vector<FormDesc>>;
    w.worldState.AttachSaveStorage(std::make_shared<Storage>(std::make_shared<FileDatabase>(path.string(),spdlog::default_logger(),true),spdlog::default_logger(),"",5));
    Inventory inv;inv.AddItem(0x3eaf2,2);a.SetInventory(inv);a.DamageActorValue(espm::ActorValue::Health,20);
    auto durable=[&](uint64_t generation){for(int i=0;i<12000;++i){w.Tick();if(w.worldState.IsChangeFormSaved(a.GetFormId(),generation))return;std::this_thread::sleep_for(std::chrono::milliseconds(1));}throw std::runtime_error("ORIGIN_SAVE_TIMEOUT");};
    durable(w.worldState.GetChangeFormGeneration(a.GetFormId()));
    if(!a.OnEquip(0x3eaf2,"crash-origin-one"))_exit(3);
    const auto generation=w.worldState.GetChangeFormGeneration(a.GetFormId());
    if(stage==1)w.Tick();if(stage==2)durable(generation);
    const char ready='R';write(signal[1],&ready,1);for(;;)pause();
   }catch(...){_exit(4);}
  }
  close(signal[1]);pollfd descriptor{signal[0],POLLIN,0};const auto ready=poll(&descriptor,1,15000);char phase=0;if(ready>0)read(signal[0],&phase,1);close(signal[0]);kill(pid,SIGKILL);int status=0;waitpid(pid,&status,0);REQUIRE(phase=='R');REQUIRE(WIFSIGNALED(status));REQUIRE(WTERMSIG(status)==SIGKILL);
  std::ifstream input(path/"changeForms"/"0.json");nlohmann::json document;input>>document;uint32_t count=0;for(const auto& entry:document.at("inv").at("entries"))if(entry.at("baseId")==0x3eaf2)count=entry.at("count").get<uint32_t>();
  REQUIRE((count==1||count==2));size_t origins=0;for(const auto& effect:document.at("effects"))if(effect.contains("srpProvenanceV1")){++origins;REQUIRE(effect.at("srpProvenanceV1").at("operationId")=="crash-origin-one");}
  REQUIRE(origins==2-count);if(stage==0)REQUIRE(count==2);if(stage==2)REQUIRE(count==1);
  REQUIRE(document.at("dynamicFields").contains("private.srpResourceMaximumsV1"));const auto maximum=document.at("dynamicFields").at("private.srpResourceMaximumsV1").at("health").get<float>();REQUIRE(maximum==Catch::Approx(count==1?144:124));
 }
}

TEST_CASE("R37 expired origin after disconnect preserves absolute resources from maximums V1", "[srp-r37]"){
 auto& w=GetPartOne();auto& a=Create(w);a.DamageActorValue(espm::ActorValue::Health,20);const auto hp=Health(a);SrpActorModifiers::ApplyFromSource(a,0x3eaf2,a,"offline-expiry");auto json=MpChangeForm::ToJson(a.GetChangeForm());
 const auto past=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()-1000;json["effects"][0]["srpProvenanceV1"]["expiresAtMs"]=past;json["effects"][0]["srpProvenanceV1"]["appliedAtMs"]=past-int64_t(json["effects"][0]["duration"].get<uint32_t>())*1000;
 simdjson::dom::parser parser;auto parsed=parser.parse(json.dump());auto element=parsed.value();const auto decoded=MpChangeForm::JsonToChangeForm(element);a.ApplyChangeForm(decoded);REQUIRE(a.GetActiveMagicEffects().Empty());REQUIRE(a.GetMaximumValues().health==Catch::Approx(124));REQUIRE(Health(a)==Catch::Approx(hp));
}

TEST_CASE("R37 identity spell and keyword CTDA use real actor records", "[srp-r37]"){
 auto& w=GetPartOne();auto& a=Create(w);auto owner=w.worldState.GetEspm().GetBrowser().LookupById(0x3eaf3);using R=SrpActorModifiers::ConditionResult;
 espm::CTDA c{};c.runOnType=espm::CTDA::RunOnTypeFlags::Subject;c.comparisonValue=1;
 auto parameter=[&](uint32_t id){memcpy(c.functionData,&id,4);};auto evaluate=[&](){return SrpActorModifiers::EvaluateConditions(a,owner,{c});};
 c.functionIndex=72;parameter(a.GetBaseId());REQUIRE(evaluate()==R::Active);parameter(0x12e49);REQUIRE(evaluate()==R::Inactive);
 c.functionIndex=264;parameter(0x12fd0);a.RemoveSpell(0x12fd0);REQUIRE(evaluate()==R::Inactive);a.AddSpell(0x12fd0);REQUIRE(evaluate()==R::Active);a.RemoveSpell(0x12fd0);REQUIRE(evaluate()==R::Inactive);
 const auto armor=w.worldState.GetEspm().GetBrowser().LookupById(0x12e49);const auto keywords=armor.rec->GetKeywordIds(w.worldState.GetEspmCache());REQUIRE_FALSE(keywords.empty());
 c.functionIndex=682;parameter(armor.ToGlobalId(keywords.front()));Inventory inv;Inventory::Entry item(0x12e49,1);inv.entries.push_back(item);a.SetInventory(inv);Equipment worn;item.SetWorn(Inventory::Worn::Right);worn.inv.entries.push_back(item);a.SetEquipment(worn);REQUIRE(evaluate()==R::Active);a.SetInventory(Inventory{});REQUIRE(evaluate()==R::Inactive);
 parameter(0x12fd0);REQUIRE(evaluate()==R::Unknown);
}


namespace {
void KnownEmptyPerks(MpActor& a){a.SetPropertyValueDump("private.srpPerkAuthorityV1",nlohmann::json{{"schemaVersion",1},{"authority","SRP-PERKS-V1"},{"complete",true},{"profileId",std::to_string(a.GetProfileId())},{"characterId","r37"},{"revision",1},{"perks",nlohmann::json::array()}}.dump(),false,false);}
void PrepareSelfSpell(MpActor& a){a.AddSpell(0x2f3b8);Equipment e;e.leftSpell=0x2f3b8;a.SetEquipment(e);KnownEmptyPerks(a);a.SetPropertyValueDump("private.srpSpellCostPolicyV1","\"RECORD_BASE_ONLY_V1\"",false,false);}
}
TEST_CASE("R37.2 CTDA health fraction worldspace and worn apparel use exact authoritative state", "[srp-r37]"){
 auto& w=GetPartOne();auto& a=Create(w);auto owner=w.worldState.GetEspm().GetBrowser().LookupById(0x3eaf3);using R=SrpActorModifiers::ConditionResult;espm::CTDA c{};c.runOnType=espm::CTDA::RunOnTypeFlags::Subject;
 c.functionIndex=640;c.comparisonValue=a.GetActorValues().healthPercentage;uint32_t av=24;memcpy(c.functionData,&av,4);REQUIRE(SrpActorModifiers::EvaluateConditions(a,owner,{c})==R::Active);a.DamageActorValue(espm::ActorValue::Health,20);REQUIRE(SrpActorModifiers::EvaluateConditions(a,owner,{c})==R::Inactive);av=39;memcpy(c.functionData,&av,4);REQUIRE(SrpActorModifiers::EvaluateConditions(a,owner,{c})==R::Unknown);
 c.functionIndex=310;c.comparisonValue=1;uint32_t id=0x3c;memcpy(c.functionData,&id,4);REQUIRE(SrpActorModifiers::EvaluateConditions(a,owner,{c})==R::Active);
 Inventory inv;Inventory::Entry item(0x12e49,1);inv.entries.push_back(item);a.SetInventory(inv);item.SetWorn(Inventory::Worn::Right);Equipment worn;worn.inv.entries={item,item};a.SetEquipment(worn);
 const auto armor=w.worldState.GetEspm().GetBrowser().LookupById(item.baseId);id=armor.ToGlobalId(armor.rec->GetKeywordIds(w.worldState.GetEspmCache()).front());c.functionIndex=722;memcpy(c.functionData,&id,4);REQUIRE(SrpActorModifiers::EvaluateConditions(a,owner,{c})==R::Active);c.comparisonValue=2;REQUIRE(SrpActorModifiers::EvaluateConditions(a,owner,{c})==R::Inactive);a.SetInventory(Inventory{});c.comparisonValue=0;REQUIRE(SrpActorModifiers::EvaluateConditions(a,owner,{c})==R::Active);
}
TEST_CASE("R37.2 perk authority is explicit scoped and never inferred from missing data", "[srp-r37]"){
 auto& w=GetPartOne();auto& a=Create(w);auto owner=w.worldState.GetEspm().GetBrowser().LookupById(0x3eaf3);using R=SrpActorModifiers::ConditionResult;espm::CTDA c{};c.functionIndex=448;c.runOnType=espm::CTDA::RunOnTypeFlags::Subject;c.comparisonValue=1;uint32_t perk=0x581f9;memcpy(c.functionData,&perk,4);
 REQUIRE(SrpActorModifiers::EvaluateConditions(a,owner,{c})==R::Unknown);KnownEmptyPerks(a);REQUIRE(SrpActorModifiers::EvaluateConditions(a,owner,{c})==R::Inactive);
 auto ledger=a.GetDynamicFields().GetAsJson().at("private.srpPerkAuthorityV1");ledger["perks"].push_back("581f9:Skyrim.esm");a.SetPropertyValueDump("private.srpPerkAuthorityV1",ledger.dump(),false,false);REQUIRE(SrpActorModifiers::EvaluateConditions(a,owner,{c})==R::Active);
 a.SetPropertyValueDump("srpActiveCharacterId","\"foreign\"",false,false);REQUIRE(SrpActorModifiers::EvaluateConditions(a,owner,{c})==R::Unknown);
}
TEST_CASE("R37.2 ENCH origins preserve independent actual equipment and removal", "[srp-r37]"){
 auto& w=GetPartOne();auto& a=Create(w);Inventory inv;Inventory::Entry body(0x12e49,1),boots(0x12e4b,1);body.enchantmentId=0x49509;boots.enchantmentId=0x49509;body.name="legitimate-a";boots.name="legitimate-b";inv.entries={body,boots};a.SetInventory(inv);const auto hp=Health(a);body.SetWorn(Inventory::Worn::Right);boots.SetWorn(Inventory::Worn::Right);Equipment eq;eq.inv.entries={body,boots};a.SetEquipment(eq);
 REQUIRE(a.GetMaximumValues().health==Catch::Approx(164));REQUIRE(Health(a)==Catch::Approx(hp));auto metadata=a.GetDynamicFields().GetAsJson().at("private.srpEquipmentOriginsV1");REQUIRE(metadata.at("origins").size()==2);for(auto row:metadata.at("origins")){REQUIRE(row.at("state")=="ACTIVE");REQUIRE(row.at("source")=="49509:Skyrim.esm");REQUIRE(row.at("sourceType")=="ENCH");}
 eq.inv.entries={boots};a.SetEquipment(eq);REQUIRE(a.GetMaximumValues().health==Catch::Approx(144));REQUIRE(Health(a)==Catch::Approx(hp));metadata=a.GetDynamicFields().GetAsJson().at("private.srpEquipmentOriginsV1");size_t active=0,removed=0;for(auto row:metadata.at("origins")){active+=row.at("state")=="ACTIVE";removed+=row.at("state")=="REMOVED";}REQUIRE(active==1);REQUIRE(removed==1);
 a.SetEquipment(eq);REQUIRE(a.GetDynamicFields().GetAsJson().at("private.srpEquipmentOriginsV1")==metadata);auto saved=a.GetChangeForm();a.ApplyChangeForm(saved);REQUIRE(a.GetMaximumValues().health==Catch::Approx(144));REQUIRE(Health(a)==Catch::Approx(hp));
}
TEST_CASE("R37.2 self SPEL atomically restores once debits magicka and survives replay", "[srp-r37]"){
 auto& w=GetPartOne();auto& a=Create(w);PrepareSelfSpell(a);a.DamageActorValue(espm::ActorValue::Health,50);const auto hp=Health(a),mp=a.GetActorValues().magicka*a.GetActorValues().magickaPercentage;
 const auto receipt=SrpActorModifiers::CastSelf(a,0x2f3b8,"cast-self-fast-healing-1");REQUIRE(receipt.at("source")=="2f3b8:Skyrim.esm");REQUIRE(receipt.at("cost")==73);REQUIRE(Health(a)==Catch::Approx(hp+50));REQUIRE(a.GetActorValues().magicka*a.GetActorValues().magickaPercentage==Catch::Approx(mp-73));REQUIRE(receipt.at("effects").size()==2);REQUIRE(receipt.at("effects")[0].at("state")=="INSTANT_COMPLETE");REQUIRE(receipt.at("effects")[1].at("state")=="CONDITION_FALSE");
 const auto generation=w.worldState.GetChangeFormGeneration(a.GetFormId());REQUIRE(SrpActorModifiers::CastSelf(a,0x2f3b8,"cast-self-fast-healing-1").at("duplicate")==true);REQUIRE(w.worldState.GetChangeFormGeneration(a.GetFormId())==generation);REQUIRE(Health(a)==Catch::Approx(hp+50));
 const auto json=MpChangeForm::ToJson(a.GetChangeForm());simdjson::dom::parser parser;auto parsed=parser.parse(json.dump());auto element=parsed.value();a.ApplyChangeForm(MpChangeForm::JsonToChangeForm(element));REQUIRE(SrpActorModifiers::CastSelf(a,0x2f3b8,"cast-self-fast-healing-1").at("duplicate")==true);REQUIRE(Health(a)==Catch::Approx(hp+50));
}
TEST_CASE("R37.2 SPEL rejects unknown conditions cost and source before any debit", "[srp-r37]"){
 auto& w=GetPartOne();auto& a=Create(w);a.AddSpell(0x2f3b8);Equipment eq;eq.leftSpell=0x2f3b8;a.SetEquipment(eq);const auto before=MpChangeForm::ToJson(a.GetChangeForm());
 REQUIRE_THROWS_WITH(SrpActorModifiers::CastSelf(a,0x2f3b8,"cast-unknown-cost-1"),"SRP_SPELL_COST_AUTHORITY_UNAVAILABLE");REQUIRE(MpChangeForm::ToJson(a.GetChangeForm())==before);
 a.SetPropertyValueDump("private.srpSpellCostPolicyV1","\"RECORD_BASE_ONLY_V1\"",false,false);const auto unknown=MpChangeForm::ToJson(a.GetChangeForm());REQUIRE_THROWS_WITH(SrpActorModifiers::CastSelf(a,0x2f3b8,"cast-unknown-perk-1"),"SRP_SPELL_CONDITIONS_UNKNOWN");REQUIRE(MpChangeForm::ToJson(a.GetChangeForm())==unknown);
 KnownEmptyPerks(a);a.DamageActorValue(espm::ActorValue::Magicka,99);const auto depleted=MpChangeForm::ToJson(a.GetChangeForm());REQUIRE_THROWS_WITH(SrpActorModifiers::CastSelf(a,0x2f3b8,"cast-no-magicka-1"),"SRP_SPELL_INSUFFICIENT_MAGICKA");REQUIRE(MpChangeForm::ToJson(a.GetChangeForm())==depleted);
}

TEST_CASE("R37.2 self spell receipt magicka and restoration remain atomic across SIGKILL", "[srp-r37][crash]"){
 namespace fs=std::filesystem;
 for(int stage=0;stage<3;++stage){
  const auto path=fs::temp_directory_path()/("srp-r372-spell-crash-"+std::to_string(getpid())+"-"+std::to_string(stage));fs::create_directories(path);int fd[2];REQUIRE(pipe(fd)==0);const auto pid=fork();REQUIRE(pid>=0);
  if(pid==0){close(fd[0]);try{
   auto& w=GetPartOne();auto& a=Create(w);PrepareSelfSpell(a);a.DamageActorValue(espm::ActorValue::Health,40);
   using Storage=Viet::AsyncSaveStorage<MpChangeForm,FormDesc,std::vector<FormDesc>>;w.worldState.AttachSaveStorage(std::make_shared<Storage>(std::make_shared<FileDatabase>(path.string(),spdlog::default_logger(),true),spdlog::default_logger(),"",5));
   auto durable=[&](uint64_t generation){for(int i=0;i<12000;++i){w.Tick();if(w.worldState.IsChangeFormSaved(a.GetFormId(),generation))return;std::this_thread::sleep_for(std::chrono::milliseconds(1));}throw std::runtime_error("SPELL_SAVE_TIMEOUT");};
   a.SetPropertyValueDump("private.srpSpellCrashBaseline","true",false,false);durable(w.worldState.GetChangeFormGeneration(a.GetFormId()));
   SrpActorModifiers::CastSelf(a,0x2f3b8,"cast-self-crash-test-1");const auto generation=w.worldState.GetChangeFormGeneration(a.GetFormId());if(stage==1)w.Tick();if(stage==2)durable(generation);const char ready='R';write(fd[1],&ready,1);for(;;)pause();
  }catch(...){_exit(4);}}
  close(fd[1]);pollfd descriptor{fd[0],POLLIN,0};const auto ready=poll(&descriptor,1,15000);char phase=0;if(ready>0)read(fd[0],&phase,1);close(fd[0]);kill(pid,SIGKILL);int status=0;waitpid(pid,&status,0);REQUIRE(phase=='R');REQUIRE(WIFSIGNALED(status));
  std::ifstream input(path/"changeForms"/"0.json");nlohmann::json document;input>>document;const auto fields=document.at("dynamicFields");const bool committed=fields.contains("private.srpSpellReceiptsV1");const auto max=fields.at("private.srpResourceMaximumsV1");
  const auto health=document.at("healthPercentage").get<float>()*max.at("health").get<float>(),magicka=document.at("magickaPercentage").get<float>()*max.at("magicka").get<float>();REQUIRE(health==Catch::Approx(committed?110:60));REQUIRE(magicka==Catch::Approx(committed?27:100));if(stage==0)REQUIRE_FALSE(committed);if(stage==2)REQUIRE(committed);
  if(committed)REQUIRE(fields.at("private.srpSpellReceiptsV1").at("receipts").at("cast-self-crash-test-1").at("cost")==73);
 }
}

TEST_CASE("R37.2 death dispels only eligible provenanced temporary effects", "[srp-r37]"){
 auto& w=GetPartOne();auto& a=Create(w);SrpActorModifiers::ApplyFromSource(a,0x3eaf2,a,"death-origin-one");REQUIRE(a.GetMaximumValues().health==Catch::Approx(144));a.SetIsDead(true);REQUIRE(a.GetActiveMagicEffects().Empty());REQUIRE(a.GetMaximumValues().health==Catch::Approx(124));REQUIRE(Health(a)==0);
}
TEST_CASE("R37.2 valid caster disconnection never erases another caster instance", "[srp-r37]"){
 auto& w=GetPartOne();auto& a=Create(w);w.CreateActor(0xff000001,{0,0,0},0,0x3c,2);auto& b=w.worldState.GetFormAt<MpActor>(0xff000001);auto effect=SourceSpell(w,0xfb994,0);auto own=SrpActorModifiers::CreateProvenance(a,0xfb994,0,a,"own-after-disconnect"),other=SrpActorModifiers::CreateProvenance(a,0xfb994,0,b,"other-before-disconnect");a.ApplyMagicEffect(effect,false,false,&own);a.ApplyMagicEffect(effect,false,false,&other);const auto stored=a.GetActiveMagicEffects().ToJson();w.worldState.DestroyForm<MpActor>(b.GetFormId());REQUIRE(a.GetActiveMagicEffects().ToJson()==stored);REQUIRE(a.GetMaximumValues().health==Catch::Approx(149));REQUIRE(a.GetActiveMagicEffects().GetEntries().size()==2);auto restored=a.GetChangeForm();restored.activeMagicEffects.RemoveByStackKey(espm::ActorValue::Health,other.at("stackKey").get<std::string>());a.ApplyChangeForm(restored);REQUIRE(a.GetActiveMagicEffects().GetEntries().size()==1);REQUIRE(a.GetActiveMagicEffects().GetByStackKey(espm::ActorValue::Health,own.at("stackKey").get<std::string>()).has_value());
}

TEST_CASE("R37.2 CTDA keyword faction and death states have native evidence", "[srp-r37]"){
 auto& w=GetPartOne();auto& a=Create(w);auto owner=w.worldState.GetEspm().GetBrowser().LookupById(0x3eaf3);using R=SrpActorModifiers::ConditionResult;espm::CTDA c{};c.runOnType=espm::CTDA::RunOnTypeFlags::Subject;c.comparisonValue=1;
 const auto race=w.worldState.GetEspm().GetBrowser().LookupById(a.GetRaceId());const auto keys=race.rec->GetKeywordIds(w.worldState.GetEspmCache());REQUIRE_FALSE(keys.empty());auto id=race.ToGlobalId(keys.front());c.functionIndex=560;memcpy(c.functionData,&id,4);REQUIRE(SrpActorModifiers::EvaluateConditions(a,owner,{c})==R::Active);
 id=0xdb1;REQUIRE(w.worldState.GetEspm().GetBrowser().LookupById(id).rec->GetType()==espm::FACT::kType);c.functionIndex=71;memcpy(c.functionData,&id,4);a.AddToFaction({FormDesc::FromFormId(id,w.worldState.espmFiles),0});REQUIRE(SrpActorModifiers::EvaluateConditions(a,owner,{c})==R::Active);const auto membership=MpChangeForm::ToJson(a.GetChangeForm());a.AddToFaction({FormDesc::FromFormId(id,w.worldState.espmFiles),7});REQUIRE(MpChangeForm::ToJson(a.GetChangeForm())==membership);a.RemoveFromFaction(FormDesc::FromFormId(id,w.worldState.espmFiles));REQUIRE(SrpActorModifiers::EvaluateConditions(a,owner,{c})==R::Inactive);
 c.functionIndex=46;id=0;memcpy(c.functionData,&id,4);REQUIRE(SrpActorModifiers::EvaluateConditions(a,owner,{c})==R::Inactive);a.SetIsDead(true);REQUIRE(SrpActorModifiers::EvaluateConditions(a,owner,{c})==R::Active);
}

TEST_CASE("R37.2 aborted recast retains the original instance expiration timer", "[srp-r37]"){
 auto& w=GetPartOne();auto& a=Create(w);espm::Effects::Effect initial{0x3eaf3,20,0,1},replacement{0x3eaf3,40,0,60};a.ApplyMagicEffect(initial);const auto hp=Health(a);
 REQUIRE_THROWS(a.RunChangeFormTransaction([&](){a.ApplyMagicEffect(replacement);throw std::runtime_error("FAIL_AFTER_RECAST");return true;}));REQUIRE(a.GetMaximumValues().health==Catch::Approx(144));std::this_thread::sleep_for(std::chrono::milliseconds(1100));w.Tick();REQUIRE(a.GetActiveMagicEffects().Empty());REQUIRE(a.GetMaximumValues().health==Catch::Approx(124));REQUIRE(Health(a)==Catch::Approx(hp));
}

TEST_CASE("R37.3 real SPEL cost metadata never substitutes for final engine authority", "[srp-r37]"){
 auto& w=GetPartOne();auto& a=Create(w);PrepareSelfSpell(a);const auto before=MpChangeForm::ToJson(a.GetChangeForm());
 auto report=SrpActorModifiers::SpellAuthority(a,0x2f3b8);
 REQUIRE(report.at("recordBaseCost")==73);REQUIRE(report.at("finalCost").is_null());REQUIRE(report.at("applicationAllowed")==false);REQUIRE(report.at("engineControlSupported")==false);REQUIRE(report.at("effects").size()==2);REQUIRE(report.at("effects")[0].at("school").is_number_integer());
 REQUIRE(SrpActorModifiers::SpellAuthority(a,0x2f3b8,true).at("dualCast")==true);
 REQUIRE(MpChangeForm::ToJson(a.GetChangeForm())==before);REQUIRE_THROWS(SrpActorModifiers::SpellAuthority(a,0x12e49));
}

TEST_CASE("R37.3 remote outbox caster receipt does not erase target effect metadata", "[srp-r37]"){
 auto& w=GetPartOne();auto& a=Create(w);PrepareSelfSpell(a);const auto self=SrpActorModifiers::CastSelf(a,0x2f3b8,"cast-r373-legacy-identity");
 auto receipt=self;receipt["operationId"]="cast-r373-remote-receipt";receipt["applicationType"]="REMOTE_OUTBOX_V1";receipt["target"]["characterId"]="other-character";receipt["effects"]=nlohmann::json::array({{{"state","ACTIVE"},{"origin",{{"instanceId","target-instance"}}}}});
 a.SetPropertyValueDump("private.srpSpellReceiptsV1",nlohmann::json{{"schemaVersion",1},{"receipts",{{"cast-r373-remote-receipt",receipt}}}}.dump(),false,false);
 const auto before=MpChangeForm::ToJson(a.GetChangeForm());REQUIRE(SrpActorModifiers::SpellReceipt(a,"cast-r373-remote-receipt")==receipt);REQUIRE(MpChangeForm::ToJson(a.GetChangeForm())==before);
 a.SetPropertyValueDump("srpActiveCharacterId","\"replacement\"",false,false);REQUIRE_THROWS(SrpActorModifiers::SpellReceipt(a,"cast-r373-remote-receipt"));
}
