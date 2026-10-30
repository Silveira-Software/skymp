#include "SrpOriginalStats.h"
#include "EvaluateTemplate.h"
#include "MpActor.h"
#include "SrpOriginalLifecycle.h"
#include "libespm/RecordHeaderAccess.h"
#include "libespm/ReadUnaligned.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <sodium.h>

namespace {
void Require(bool condition, const char* reason)
{
  if (!condition) throw std::runtime_error(reason);
}
void Valid(const BaseActorValues& v)
{
  Require(std::isfinite(v.health) && v.health > 0 && v.health <= 100000 &&
    std::isfinite(v.magicka) && v.magicka >= 0 && v.magicka <= 100000 &&
    std::isfinite(v.stamina) && v.stamina > 0 && v.stamina <= 100000,
    "SRP_ORIGINAL_STATS_RESOURCE_RANGE");
}
std::string Digest(nlohmann::json snapshot)
{
  snapshot.erase("checksum");const auto text=snapshot.dump();
  unsigned char digest[crypto_hash_sha256_BYTES];
  crypto_hash_sha256(digest,reinterpret_cast<const unsigned char*>(text.data()),text.size());
  constexpr char hex[]="0123456789abcdef";std::string result;
  for(auto byte:digest){result+=hex[byte>>4];result+=hex[byte&15];}
  return result;
}
}

BaseActorValues SrpOriginalStats::Allocate(BaseActorValues base, uint16_t level,
  const std::array<uint8_t,3>& weights, float healthLevelBonus)
{
  for (float value : {base.health,base.magicka,base.stamina})
    Require(std::isfinite(value) && std::abs(value)<=1000000,
      "SRP_ORIGINAL_STATS_RESOURCE_RANGE");
  Require(level > 0 && level <= 1000 && std::isfinite(healthLevelBonus) &&
    healthLevelBonus >= 0 && healthLevelBonus <= 1000,
    "SRP_ORIGINAL_STATS_LEVEL_OR_GMST_INVALID");
  const uint32_t sum = uint32_t(weights[0]) + weights[1] + weights[2];
  Require(sum > 0, "SRP_ORIGINAL_STATS_CLASS_WEIGHTS_EMPTY");
  // Only the unambiguous integral distribution is admitted for now. Fractional
  // class weights need a captured AE engine rounding fixture, not a guessed
  // rounding rule. This check also covers level one (zero growth).
  const uint32_t budget = 10u * (level - 1u);
  std::array<uint32_t,3> growth{};
  for (size_t i = 0; i < 3; ++i) {
    Require((budget * weights[i]) % sum == 0,
      "SRP_ORIGINAL_STATS_ENGINE_ROUNDING_UNVERIFIED");
    growth[i] = budget * weights[i] / sum;
  }
  base.health += growth[0] + (level - 1u) * healthLevelBonus;
  base.magicka += growth[1];
  base.stamina += growth[2];
  Valid(base);
  return base;
}

nlohmann::json SrpOriginalStats::Preview(const MpActor& actor)
{
  auto* world = actor.GetParent();
  Require(world && world->HasEspm(), "SRP_ORIGINAL_STATS_ESPM_REQUIRED");
  const auto life = SrpOriginalLifecycle::Inspect(actor);
  Require(life.at("state") == "ALIVE_OBSERVED" && life.at("saved") == true,
    "SRP_ORIGINAL_STATS_SAVED_ALIVE_IDENTITY_REQUIRED");
  const auto stats = EvaluateTemplate<espm::NPC_::UseStats>(world,
    actor.GetBaseId(), actor.GetChangeForm().templateChain,
    [](const auto& record,const auto& data){return std::make_pair(record,data);});
  const auto& data = stats.second;
  Require(data.hasStatsConfiguration, "SRP_ORIGINAL_STATS_ACBS_TRUNCATED");
  // Choosing an observer's level here would make scaling change after host
  // handoff. PC-multiplier NPCs need the persisted CELL/EZ encounter producer.
  Require(!data.pcLevelMult, "SRP_ORIGINAL_STATS_ENCOUNTER_LEVEL_REQUIRED");
  Require(data.level > 0 && data.level <= 1000,
    "SRP_ORIGINAL_STATS_FIXED_LEVEL_INVALID");
  const auto raceId=EvaluateTemplate<espm::NPC_::UseTraits>(world,actor.GetBaseId(),
    actor.GetChangeForm().templateChain,
    [](const auto& record,const auto& npc){return record.ToGlobalId(npc.race);});
  Require(raceId==actor.GetRaceId(),"SRP_ORIGINAL_STATS_RACE_OVERRIDE_REQUIRES_REVIEW");
  auto values = GetBaseActorValues(world, actor.GetBaseId(), raceId,
    actor.GetChangeForm().templateChain);
  const auto race=espm::GetData<espm::RACE>(raceId,world);
  // The legacy player path has fallback clamps. A negative authored offset
  // must be added BEFORE growth, not replaced by the legacy fallback value.
  values.health=race.startingHealth+data.healthOffset;
  values.magicka=race.startingMagicka+data.magickaOffset;
  values.stamina=race.startingStamina+data.staminaOffset;
  nlohmann::json weightsJson = nullptr, classDesc = nullptr, bonusJson = nullptr, bonusSource=nullptr;
  if (data.autoCalcStats) {
    const auto classId = stats.first.ToGlobalId(data.classId);
    const auto klass = world->GetEspm().GetBrowser().LookupById(classId);
    Require(klass.rec && klass.rec->GetType() == "CLAS", "SRP_ORIGINAL_STATS_CLASS_REQUIRED");
    std::array<uint8_t,3> weights{};bool classFound=false;
    espm::RecordHeaderAccess::IterateFields(klass.rec,
      [&](const char* type,uint32_t size,const char* bytes){
        if (!memcmp(type,"DATA",4) && size >= 36) {
          std::copy_n(reinterpret_cast<const uint8_t*>(bytes)+32,3,weights.begin());
          classFound=true;
        }
      },world->GetEspmCache());
    Require(classFound,"SRP_ORIGINAL_STATS_CLASS_TRUNCATED");
    std::optional<float> bonus;
    for (const auto& record : world->GetEspm().GetBrowser().GetDistinctRecordsByType("GMST")) {
      if (strcmp(record.rec->GetEditorId(world->GetEspmCache()),"fNPCHealthLevelBonus")) continue;
      Require(bonusSource.is_null(),"SRP_ORIGINAL_STATS_GMST_AMBIGUOUS");
      bonusSource=FormDesc::FromFormId(record.ToGlobalId(record.rec->GetId()),world->espmFiles).ToString();
      espm::RecordHeaderAccess::IterateFields(record.rec,
        [&](const char* type,uint32_t size,const char* bytes){
          if (!memcmp(type,"DATA",4) && size == 4) bonus=espm::ReadFieldScalar<float>(bytes,size);
        },world->GetEspmCache());
    }
    Require(bonus.has_value(),"SRP_ORIGINAL_STATS_GMST_REQUIRED");
    values=Allocate(values,data.level,weights,*bonus);
    weightsJson=weights;bonusJson=*bonus;
    classDesc=FormDesc::FromFormId(classId,world->espmFiles).ToString();
  }
  Valid(values);
  nlohmann::json snapshot={{"schemaVersion",1},{"identity",life.at("identity")},
    {"statsSource",FormDesc::FromFormId(stats.first.ToGlobalId(stats.first.rec->GetId()),world->espmFiles).ToString()},
    {"race",FormDesc::FromFormId(raceId,world->espmFiles).ToString()},
    {"level",data.level},{"autoCalcStats",data.autoCalcStats},{"class",classDesc},
    {"attributeWeights",weightsJson},{"healthLevelBonus",bonusJson},{"healthLevelBonusSource",bonusSource},
    {"maximums",{{"health",values.health},{"magicka",values.magicka},{"stamina",values.stamina}}},
    {"engineParity","HUMAN_PENDING"},{"gameplayEnabled",false}};
  snapshot["checksum"]=Digest(snapshot);
  return snapshot;
}

std::optional<BaseActorValues> SrpOriginalStats::Read(const MpActor& actor)
{
  const auto& fields=actor.GetDynamicFields().GetAsJson();
  if (!fields.contains(kProperty)) return std::nullopt;
  const auto& snapshot=fields.at(kProperty);
  const auto life=SrpOriginalLifecycle::Inspect(actor);
  Require(snapshot.is_object() && snapshot.value("schemaVersion",0)==1 &&
    snapshot.value("checksum",std::string())==Digest(snapshot) &&
    snapshot.at("identity")==life.at("identity") && snapshot.value("gameplayEnabled",true)==false &&
    snapshot.at("race")==FormDesc::FromFormId(actor.GetRaceId(),actor.GetParent()->espmFiles).ToString() &&
    snapshot.at("maximums").is_object(),"SRP_ORIGINAL_STATS_SNAPSHOT_CORRUPT");
  auto value=GetBaseActorValues(actor.GetParent(),actor.GetBaseId(),actor.GetRaceId(),actor.GetChangeForm().templateChain);
  value.health=snapshot.at("maximums").at("health").get<float>();
  value.magicka=snapshot.at("maximums").at("magicka").get<float>();
  value.stamina=snapshot.at("maximums").at("stamina").get<float>();
  Valid(value);
  return value;
}
