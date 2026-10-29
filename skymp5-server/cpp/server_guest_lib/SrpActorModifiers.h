#pragma once
#include "GetBaseActorValues.h"
#include "libespm/espm.h"
#include "libespm/CTDA.h"
#include "libespm/LookupResult.h"
#include <map>
#include <vector>
class MpActor;
struct SrpActorModifiers
{
  bool complete=true;
  std::map<espm::ActorValue,float> equipment;
  std::map<espm::ActorValue,float> temporary;
  std::map<espm::ActorValue,float> passive;
  std::vector<nlohmann::json> equipmentEffects;
  static bool Managed(const MpActor& actor);
  static bool Supported(espm::ActorValue av);
  enum class ConditionResult { Inactive, Active, Unknown };
  // Unknown is never treated as false/true: the projection must stay gated.
  static ConditionResult EvaluateConditions(const MpActor& actor,
    const espm::LookupResult& owner, const std::vector<espm::CTDA>& conditions,
    const MpActor* caster=nullptr);
  static nlohmann::json CreateProvenance(MpActor& target,uint32_t source,
    uint32_t effectIndex,MpActor& caster,const std::string& operationId);
  static void ApplyFromSource(MpActor& target,uint32_t source,MpActor& caster,
    const std::string& operationId,bool hasSweetpie=false);
  static nlohmann::json CastSelf(MpActor& caster,uint32_t spell,const std::string& opId);
  static nlohmann::json SpellReceipt(MpActor& actor,const std::string& opId);
  static nlohmann::json SpellAuthority(const MpActor& actor,uint32_t spell,bool dual=false);
  static void ReconcileOrigins(MpActor& actor);
  static SrpActorModifiers Read(const MpActor& actor,bool preparingProjection=false);
  static bool Apply(MpActor& actor, espm::Effects::Effect& effect, bool restoring,
                    const nlohmann::json* provenance=nullptr);
  float Sum(espm::ActorValue av) const;
  BaseActorValues Compose(BaseActorValues permanent) const;
};
