#include "SrpActorStatsBinding.h"
#include "NapiHelper.h"
#include "SrpActorStats.h"
#include "SrpActorModifiers.h"

Napi::Value SrpActorStatsBinding::Get(Napi::Env env, ScampServer& server,
                                     uint32_t actorId)
{
  auto& actor = server.GetPartOne()->worldState.GetFormAt<MpActor>(actorId);
  // World/conditions can change without an equipment mutation. Reconcile the
  // absolute resources before publishing the newly evaluated maximums. A save
  // generation is produced only when those maximums actually changed.
  if(SrpActorModifiers::Managed(actor))actor.ReconcileSrpMaximums(actor.GetActorValues());
  auto stats = SrpActorStats::Read(actor);
  auto base = actor.GetBaseValues();
  auto maximum = actor.GetMaximumValues();
  auto permanent=stats.ComposeMaximums(base);auto modifiers=SrpActorModifiers::Read(actor,true);
  auto result = stats.ToJson();
  result["enabled"] = stats.enabled;
  result["serverAuthoritative"] = true;
  result["base"] = { { "health", base.health }, { "magicka", base.magicka },
                     { "stamina", base.stamina } };
  result["maximum"] = { { "health", maximum.health },
                        { "magicka", maximum.magicka },
                        { "stamina", maximum.stamina },
                        { "carryBonus", stats.CarryBonus() } };
  result["modifierContract"]="SRP-MODIFIERS-R37";result["modifierCompositionSupported"]=modifiers.complete;
  size_t knownOrigins=0,legacyOrigins=0;
  for(const auto& effect:actor.GetActiveMagicEffects().GetEntries())if(effect.provenance)++knownOrigins;else ++legacyOrigins;
  result["effectProvenanceVersion"]=1;
  result["effectOrigins"]={{"known",knownOrigins},{"legacyUnknown",legacyOrigins}};
  result["permanent"]={{"health",permanent.health},{"magicka",permanent.magicka},{"stamina",permanent.stamina},{"carryWeight",300.f+stats.CarryBonus()}};
  result["current"]={{"health",maximum.health*actor.GetActorValues().healthPercentage},{"magicka",maximum.magicka*actor.GetActorValues().magickaPercentage},{"stamina",maximum.stamina*actor.GetActorValues().staminaPercentage}};
  result["maximum"]["carryWeight"]=std::clamp(300.f+stats.CarryBonus()+modifiers.Sum(espm::ActorValue::CarryWeight),0.f,100000.f);
  result["modifiers"]={{"carryWeight",modifiers.Sum(espm::ActorValue::CarryWeight)},{"equipmentCarryWeight",modifiers.equipment[espm::ActorValue::CarryWeight]},{"temporaryCarryWeight",modifiers.temporary[espm::ActorValue::CarryWeight]},{"passiveCarryWeight",modifiers.passive[espm::ActorValue::CarryWeight]},{"armor",modifiers.Sum(espm::ActorValue::DamageResist)},{"magicResistance",modifiers.Sum(espm::ActorValue::MagicResist)}};
  return NapiHelper::ParseJson(env, result.dump());
}

void SrpActorStatsBinding::Set(Napi::Env env, ScampServer& server,
                              uint32_t actorId, Napi::Value value)
{
  auto& actor = server.GetPartOne()->worldState.GetFormAt<MpActor>(actorId);
  const auto input = nlohmann::json::parse(NapiHelper::Stringify(env, value));
  actor.ApplySrpActorStats(input);
}
