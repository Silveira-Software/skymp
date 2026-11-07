#include "SrpOriginalStats.h"
#include "EvaluateTemplate.h"
#include "MpActor.h"
#include "SrpOriginalLifecycle.h"
#include "SrpOriginalEncounter.h"
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
template<class T>
std::pair<T,nlohmann::json> Setting(WorldState& world, const char* editorId)
{
  std::optional<T> value;
  nlohmann::json source=nullptr;
  for (const auto& record : world.GetEspm().GetBrowser().GetDistinctRecordsByType("GMST")) {
    if (strcmp(record.rec->GetEditorId(world.GetEspmCache()),editorId)) continue;
    Require(source.is_null(),"SRP_ORIGINAL_STATS_GMST_AMBIGUOUS");
    source=FormDesc::FromFormId(record.ToGlobalId(record.rec->GetId()),world.espmFiles).ToString();
    espm::RecordHeaderAccess::IterateFields(record.rec,
      [&](const char* type,uint32_t size,const char* bytes){
        if (!memcmp(type,"DATA",4)) {
          Require(!value.has_value() && size==sizeof(T),"SRP_ORIGINAL_STATS_GMST_TRUNCATED_OR_DUPLICATE");
          value=espm::ReadFieldScalar<T>(bytes,size);
        }
      },world.GetEspmCache());
  }
  Require(value.has_value(),"SRP_ORIGINAL_STATS_GMST_REQUIRED");
  return {*value,source};
}
}

BaseActorValues SrpOriginalStats::Allocate(BaseActorValues base, uint16_t level,
  const std::array<uint8_t,3>& weights, float healthLevelBonus,
  uint32_t resourceLevelUp, const std::array<int16_t,3>& offsets)
{
  for (float value : {base.health,base.magicka,base.stamina})
    Require(std::isfinite(value) && std::abs(value)<=1000000,
      "SRP_ORIGINAL_STATS_RESOURCE_RANGE");
  Require(level > 0 && level <= 1000 && std::isfinite(healthLevelBonus) &&
    healthLevelBonus >= 0 && healthLevelBonus <= 1000 && resourceLevelUp <= 1000,
    "SRP_ORIGINAL_STATS_LEVEL_OR_GMST_INVALID");
  const uint32_t sum = uint32_t(weights[0]) + weights[1] + weights[2];
  // AE 1.6.1170: 3bf890 initializes signed 16-bit caches with truncations;
  // 3c5140 allocates floor(budget/sum)*weight, then gives the remainder one
  // point per positive weight in descending order. Equal weights are inserted
  // before existing entries: Stamina, Magicka, Health for an equal-weight tie.
  // Zero total weight skips growth; the health-level bonus still applies.
  const uint32_t budget = resourceLevelUp * (level - 1u);
  std::array<uint32_t,3> growth{};
  if (sum) {
    for (size_t i=0;i<3;++i) growth[i]=(budget/sum)*weights[i];
    auto remaining=budget%sum;
    std::array<size_t,3> order{0,1,2};
    std::sort(order.begin(),order.end(),[&](size_t a,size_t b){
      return weights[a]!=weights[b]?weights[a]>weights[b]:a>b;
    });
    auto available=weights;
    while(remaining) {
      for (auto i:order) if (available[i] && remaining) {
        ++growth[i];--available[i];--remaining;
      }
    }
  }
  std::array<float*,3> values{&base.health,&base.magicka,&base.stamina};
  for (size_t i=0;i<3;++i) {
    const auto initial=std::trunc(*values[i])+offsets[i]+
      (i==0?std::trunc((level-1u)*healthLevelBonus):0.f);
    // The engine wraps these signed caches. Refuse overflow rather than turn
    // an overflowing negative cache into a different server resource value.
    Require(initial>=-32768 && initial<=32767 && initial+growth[i]<=32767,
      "SRP_ORIGINAL_STATS_ENGINE_CACHE_OVERFLOW");
    *values[i]=initial+growth[i];
  }
  Valid(base);
  return base;
}

uint16_t SrpOriginalStats::ResolveLevel(const espm::NPC_::Data& npc,
  std::optional<uint16_t> pcLevel)
{
  Require(npc.hasStatsConfiguration,"SRP_ORIGINAL_STATS_ACBS_TRUNCATED");
  if (!npc.pcLevelMult) {
    Require(npc.level>0 && npc.level<=1000,"SRP_ORIGINAL_STATS_FIXED_LEVEL_INVALID");
    return npc.level;
  }
  Require(pcLevel.has_value(),"SRP_ORIGINAL_STATS_ENCOUNTER_LEVEL_REQUIRED");
  Require(*pcLevel>0 && *pcLevel<=1000 && npc.level>0 &&
    (!npc.calcMaxLevel || npc.calcMinLevel<=npc.calcMaxLevel),
    "SRP_ORIGINAL_STATS_ENCOUNTER_LEVEL_INVALID");
  // Match the two MULSS and CVTTSS2SI at AE RVA 1d7d33, not integer rounding
  // or a double-precision approximation. Do not emulate 16-bit overflow.
  const float product=float(*pcLevel)*float(npc.level);
  const float scaled=product*0.001f;
  Require(scaled<65536.f,"SRP_ORIGINAL_STATS_ENGINE_LEVEL_OVERFLOW");
  uint32_t level=uint32_t(scaled);
  if (npc.calcMinLevel && level<npc.calcMinLevel) level=npc.calcMinLevel;
  else if (npc.calcMaxLevel && level>npc.calcMaxLevel) level=npc.calcMaxLevel;
  Require(level>0 && level<=1000,"SRP_ORIGINAL_STATS_ENCOUNTER_LEVEL_INVALID");
  return uint16_t(level);
}

bool SrpOriginalStats::SameSnapshot(nlohmann::json existing,nlohmann::json proposed)
{
  if (existing==proposed) return true;
  // Only the old ten-point composer is equivalent. Never migrate resource
  // amounts, identity, sources or other metadata just to pass a retry.
  if (!existing.contains("resourceLevelUp") && !existing.contains("resourceLevelUpSource") &&
      proposed.value("resourceLevelUp",nlohmann::json())==10) {
    existing.erase("checksum");proposed.erase("checksum");
    proposed.erase("resourceLevelUp");proposed.erase("resourceLevelUpSource");
    return existing==proposed;
  }
  return false;
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
  // Frozen authoritative context, not the current host/observer's level.
  const auto encounter=data.pcLevelMult?SrpOriginalEncounter::Read(actor,true):std::nullopt;
  Require(!data.pcLevelMult || encounter.has_value(),"SRP_ORIGINAL_STATS_ENCOUNTER_LEVEL_REQUIRED");
  const auto level=encounter?encounter->at("finalLevel").get<uint16_t>():ResolveLevel(data,std::nullopt);
  if(encounter){
    const auto& stored=encounter->at("stats");
    nlohmann::json chain=nlohmann::json::array();
    for(const auto& desc:actor.GetChangeForm().templateChain)chain.push_back(desc.ToString());
    Require(stored.at("source")==FormDesc::FromFormId(stats.first.ToGlobalId(stats.first.rec->GetId()),world->espmFiles).ToString() &&
      stored.at("pcLevelMult")==data.pcLevelMult && stored.at("value")==data.level &&
      stored.at("minimum")==data.calcMinLevel && stored.at("maximum")==data.calcMaxLevel && stored.at("templates")==chain,
      "SRP_ORIGINAL_STATS_ENCOUNTER_CONFIG_CHANGED");
  }
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
  nlohmann::json weightsJson = nullptr, classDesc = nullptr, bonusJson = nullptr,
    bonusSource=nullptr,resourceLevelUp=nullptr,resourceLevelUpSource=nullptr;
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
    const auto bonus=Setting<float>(*world,"fNPCHealthLevelBonus");
    const auto levelUp=Setting<int32_t>(*world,"iAVDhmsLevelUp");
    Require(levelUp.first>=0 && levelUp.first<=1000,"SRP_ORIGINAL_STATS_LEVEL_OR_GMST_INVALID");
    values.health=race.startingHealth;values.magicka=race.startingMagicka;values.stamina=race.startingStamina;
    values=Allocate(values,level,weights,bonus.first,uint32_t(levelUp.first),
      {data.healthOffset,data.magickaOffset,data.staminaOffset});
    weightsJson=weights;bonusJson=bonus.first;bonusSource=bonus.second;
    resourceLevelUp=levelUp.first;resourceLevelUpSource=levelUp.second;
    classDesc=FormDesc::FromFormId(classId,world->espmFiles).ToString();
  }
  Valid(values);
  nlohmann::json snapshot={{"schemaVersion",1},{"identity",life.at("identity")},
    {"statsSource",FormDesc::FromFormId(stats.first.ToGlobalId(stats.first.rec->GetId()),world->espmFiles).ToString()},
    {"race",FormDesc::FromFormId(raceId,world->espmFiles).ToString()},
    {"level",level},{"autoCalcStats",data.autoCalcStats},{"class",classDesc},
    {"attributeWeights",weightsJson},{"healthLevelBonus",bonusJson},{"healthLevelBonusSource",bonusSource},
    {"maximums",{{"health",values.health},{"magicka",values.magicka},{"stamina",values.stamina}}},
    {"engineParity","HUMAN_PENDING"},{"gameplayEnabled",false}};
  if(data.autoCalcStats) {
    snapshot["resourceLevelUp"]=resourceLevelUp;
    snapshot["resourceLevelUpSource"]=resourceLevelUpSource;
  }
  if(encounter)snapshot["encounterContextChecksum"]=encounter->at("checksum");
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
  if(snapshot.contains("encounterContextChecksum")){
    const auto context=SrpOriginalEncounter::Read(actor);
    Require(context && context->at("checksum")==snapshot.at("encounterContextChecksum") &&
      context->at("finalLevel")==snapshot.at("level"),"SRP_ORIGINAL_STATS_ENCOUNTER_RECEIPT_MISMATCH");
  }
  auto value=GetBaseActorValues(actor.GetParent(),actor.GetBaseId(),actor.GetRaceId(),actor.GetChangeForm().templateChain);
  value.health=snapshot.at("maximums").at("health").get<float>();
  value.magicka=snapshot.at("maximums").at("magicka").get<float>();
  value.stamina=snapshot.at("maximums").at("stamina").get<float>();
  Valid(value);
  return value;
}
