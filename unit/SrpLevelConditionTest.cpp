#include "SrpActorModifiers.h"
#include "SrpActorStats.h"
#include "TestUtils.hpp"
PartOne& GetPartOne();
TEST_CASE("R38 GetLevel uses canonical player level without a projection update deadlock", "[srp-r38-ctda]")
{
  auto& w=GetPartOne();w.CreateActor(0xff000000,{0,0,0},0,0x3c,1);
  auto& a=w.worldState.GetFormAt<MpActor>(0xff000000);
  auto owner=w.worldState.GetEspm().GetBrowser().LookupById(0x3eaf3);
  espm::CTDA condition{};condition.functionIndex=80;condition.comparisonValue=3;
  condition.runOnType=espm::CTDA::RunOnTypeFlags::Subject;
  using R=SrpActorModifiers::ConditionResult;
  auto evaluate=[&]{return SrpActorModifiers::EvaluateConditions(a,owner,{condition});};
  REQUIRE(evaluate()==R::Unknown);
  nlohmann::json p={{"schemaVersion",2},{"rulesVersion","SRP-PROGRESSION-A1"},{"revision",1},{"level",3},{"profileId",std::to_string(a.GetProfileId())},{"characterId","ctda-player"},{"attributes",{{"vigor",0},{"strength",0},{"dexterity",0},{"intelligence",0},{"willpower",0},{"constitution",0}}}};
  a.SetPropertyValueDump("srpActiveCharacterId","\"ctda-player\"",false,false);
  a.SetPropertyValueDump("srpProgression",p.dump(),false,false);a.ApplySrpActorStats(p);
  REQUIRE(evaluate()==R::Active);
  condition.comparisonValue=10;REQUIRE(evaluate()==R::Inactive);
  p["level"]=10;p["revision"]=2;
  a.SetPropertyValueDump("srpProgression",p.dump(),false,false);
  REQUIRE(evaluate()==R::Active); // canonical leads its native projection during update
  a.ApplySrpActorStats(p);REQUIRE(evaluate()==R::Active);
  auto stale=p;stale["revision"]=1;stale["level"]=3;
  a.SetPropertyValueDump("srpProgression",stale.dump(),false,false);
  REQUIRE(evaluate()==R::Unknown); // canonical behind durable native revision is inconsistent
  a.SetPropertyValueDump("srpProgression",p.dump(),false,false);
  a.SetPropertyValueDump("srpActiveCharacterId","\"another-player\"",false,false);
  REQUIRE(evaluate()==R::Unknown);
}
