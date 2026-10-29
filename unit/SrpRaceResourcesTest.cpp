#include "SrpActorStats.h"
#include "TestUtils.hpp"
#include <catch2/catch_approx.hpp>
PartOne& GetPartOne();

TEST_CASE("R38 managed race appearance preserves absolute resources through replay restore and reload", "[srp-r38-race]")
{
  auto& w=GetPartOne();
  w.CreateActor(0xff000000,{0,0,0},0,0x3c,1);
  auto& a=w.worldState.GetFormAt<MpActor>(0xff000000);
  nlohmann::json p={{"schemaVersion",2},{"rulesVersion","SRP-PROGRESSION-A1"},{"revision",1},{"level",4},{"profileId",std::to_string(a.GetProfileId())},{"characterId","r381-race"},{"attributes",{{"vigor",10},{"strength",10},{"dexterity",10},{"intelligence",10},{"willpower",10},{"constitution",10}}}};
  a.SetPropertyValueDump("srpActiveCharacterId","\"r381-race\"",false,false);
  a.SetPropertyValueDump("srpProgression",p.dump(),false,false);a.ApplySrpActorStats(p);
  for(auto av:{espm::ActorValue::Health,espm::ActorValue::Magicka,espm::ActorValue::Stamina})a.DamageActorValue(av,20);
  const auto baseline=a.GetActorValues();
  uint32_t race=0;
  auto& browser=w.worldState.GetEspm().GetBrowser();
  for(const auto& row:browser.GetDistinctRecordsByType("REFR")){
    if(row.rec->GetType()!="ACHR")continue;
    const auto placed=reinterpret_cast<const espm::ACHR*>(row.rec)->GetData(w.worldState.GetEspmCache());
    const auto npc=browser.LookupById(row.ToGlobalId(placed.baseId));
    if(!npc.rec||npc.rec->GetType()!="NPC_")continue;
    const auto base=reinterpret_cast<const espm::NPC_*>(npc.rec)->GetData(w.worldState.GetEspmCache());
    const auto candidate=browser.LookupById(npc.ToGlobalId(base.race));
    if(!candidate.rec||candidate.rec->GetType()!="RACE")continue;
    const auto data=reinterpret_cast<const espm::RACE*>(candidate.rec)->GetData(w.worldState.GetEspmCache());
    if(data.startingHealth>baseline.health&&data.startingMagicka>=baseline.magicka&&data.startingStamina>=baseline.stamina){race=npc.ToGlobalId(base.race);break;}
  }
  REQUIRE(race!=0);
  Appearance changed;changed.raceId=race;
  const auto inv=a.GetInventory().ToJson().dump();
  auto check=[&]{const auto v=a.GetActorValues();REQUIRE(v.healthPercentage*v.health==Catch::Approx(baseline.healthPercentage*baseline.health));REQUIRE(v.magickaPercentage*v.magicka==Catch::Approx(baseline.magickaPercentage*baseline.magicka));REQUIRE(v.staminaPercentage*v.stamina==Catch::Approx(baseline.staminaPercentage*baseline.stamina));REQUIRE(a.GetInventory().ToJson().dump()==inv);};
  a.SetAppearance(&changed);REQUIRE(a.GetActorValues().health>baseline.health);check();
  a.SetAppearance(&changed);check();
  auto saved=MpChangeForm::ToJson(a.GetChangeForm());
  simdjson::dom::parser parser;auto document=parser.parse(saved.dump());auto element=document.value();auto decoded=MpChangeForm::JsonToChangeForm(element);
  a.ApplyChangeForm(decoded);check();
  a.SetAppearance(nullptr);check();
}
