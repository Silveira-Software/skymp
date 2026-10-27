#include "SrpActorStatsBinding.h"
#include "NapiHelper.h"
#include "SrpActorStats.h"

Napi::Value SrpActorStatsBinding::Get(Napi::Env env, ScampServer& server,
                                     uint32_t actorId)
{
  auto& actor = server.GetPartOne()->worldState.GetFormAt<MpActor>(actorId);
  auto stats = SrpActorStats::Read(actor);
  auto base = actor.GetBaseValues();
  auto maximum = actor.GetMaximumValues();
  auto result = stats.ToJson();
  result["enabled"] = stats.enabled;
  result["serverAuthoritative"] = true;
  result["base"] = { { "health", base.health }, { "magicka", base.magicka },
                     { "stamina", base.stamina } };
  result["maximum"] = { { "health", maximum.health },
                        { "magicka", maximum.magicka },
                        { "stamina", maximum.stamina },
                        { "carryBonus", stats.CarryBonus() } };
  return NapiHelper::ParseJson(env, result.dump());
}

void SrpActorStatsBinding::Set(Napi::Env env, ScampServer& server,
                              uint32_t actorId, Napi::Value value)
{
  auto& actor = server.GetPartOne()->worldState.GetFormAt<MpActor>(actorId);
  const auto input = nlohmann::json::parse(NapiHelper::Stringify(env, value));
  actor.ApplySrpActorStats(input);
}
