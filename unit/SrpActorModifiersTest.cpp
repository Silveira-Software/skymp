#include "SrpActorModifiers.h"
#include "SrpActorStats.h"
#include "TestUtils.hpp"
#include <catch2/catch_approx.hpp>
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
