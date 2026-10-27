#pragma once
#include "GetBaseActorValues.h"
#include "libespm/espm.h"
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
  static SrpActorModifiers Read(const MpActor& actor,bool preparingProjection=false);
  static bool Apply(MpActor& actor, espm::Effects::Effect& effect, bool restoring);
  float Sum(espm::ActorValue av) const;
  BaseActorValues Compose(BaseActorValues permanent) const;
};
