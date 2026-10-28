#pragma once
#include "GetBaseActorValues.h"
#include "libespm/espm.h"
#include "libespm/CTDA.h"
#include "libespm/LookupResult.h"
#include <map>
class MpActor;
struct SrpActorModifiers
{
  bool complete=true;
  std::map<espm::ActorValue,float> equipment;
  std::map<espm::ActorValue,float> temporary;
  std::map<espm::ActorValue,float> passive;
  static bool Managed(const MpActor& actor);
  static bool Supported(espm::ActorValue av);
  enum class ConditionResult { Inactive, Active, Unknown };
  // Unknown is never treated as false/true: the projection must stay gated.
  static ConditionResult EvaluateConditions(const MpActor& actor,
    const espm::LookupResult& owner, const std::vector<espm::CTDA>& conditions);
  static SrpActorModifiers Read(const MpActor& actor,bool preparingProjection=false);
  static bool Apply(MpActor& actor, espm::Effects::Effect& effect, bool restoring);
  float Sum(espm::ActorValue av) const;
  BaseActorValues Compose(BaseActorValues permanent) const;
};
