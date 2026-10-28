#include "ActiveMagicEffectsMap.h"
#include "JsonUtils.h"
#include <TimeUtils.h>
#include <cmath>
#include <regex>

void ActiveMagicEffectsMap::ValidateProvenance(const nlohmann::json& p)
{
 auto fail=[](){throw std::runtime_error("SRP_EFFECT_PROVENANCE_CORRUPT");};
 if(!p.is_object()||p.value("schemaVersion",0)!=1)fail();
 for(auto key:{"mgef","source","recordOwner","sourceType","instanceId","stackKey","stackPolicy","operationId"})
  if(!p.contains(key)||!p.at(key).is_string()||p.at(key).get_ref<const std::string&>().size()>2048)fail();
 static const std::regex desc("^[0-9a-fA-F]{1,6}:[^:/\\\\]+\\.(esm|esp|esl)$",std::regex::icase);
 if(!std::regex_match(p.at("mgef").get<std::string>(),desc)||!std::regex_match(p.at("source").get<std::string>(),desc))fail();
 if(p.at("sourceType")!="ALCH"&&p.at("sourceType")!="SPEL"&&p.at("sourceType")!="ENCH")fail();
 if((p.at("stackPolicy")!="REPLACE_SOURCE_EFFECT_CASTER_TARGET"&&p.at("stackPolicy")!="LEGACY_REPLACE_ACTOR_VALUE")||p.at("instanceId").get<std::string>().empty()||p.at("stackKey").get<std::string>().empty())fail();
 if(!p.contains("actorValue")||!p.at("actorValue").is_number_integer())fail();
 if(p.value("overlapPolicy",std::string())!="STRONGEST_ABSOLUTE_SAME_MGEF_TARGET")fail();
 if(!p.contains("effectIndex")||!p.at("effectIndex").is_number_unsigned()||p.at("effectIndex").get<uint64_t>()>4095)fail();
 if(!p.contains("appliedAtMs")||!p.at("appliedAtMs").is_number_integer()||p.at("appliedAtMs").get<int64_t>()<0)fail();
 if(!p.contains("expiresAtMs")||!p.at("expiresAtMs").is_number_integer()||p.at("expiresAtMs").get<int64_t>()<p.at("appliedAtMs").get<int64_t>())fail();
 for(auto key:{"caster","target"}){
  if(!p.contains(key)||!p.at(key).is_object())fail();const auto& a=p.at(key);
  for(auto field:{"reference","base","profileId","characterId","incarnation"})
   if(!a.contains(field)||!a.at(field).is_string()||a.at(field).get_ref<const std::string&>().size()>256)fail();
  if(a.at("reference").get<std::string>().empty()||a.at("incarnation").get<std::string>().empty())fail();
 }
 auto expected=p.at("stackPolicy")=="LEGACY_REPLACE_ACTOR_VALUE"?"legacy-av:"+std::to_string(p.at("actorValue").get<int>()):nlohmann::json::array({p.at("sourceType"),p.at("source"),p.at("effectIndex"),p.at("caster"),p.at("target")}).dump();
 if(p.at("stackKey")!=expected)fail();
}
std::string ActiveMagicEffectsMap::StackKey(const Entry& e)
{
 return e.provenance?e.provenance->at("stackKey").get<std::string>():"legacy:"+std::to_string(e.data.effectId);
}

std::optional<std::reference_wrapper<const ActiveMagicEffectsMap::Entry>>
ActiveMagicEffectsMap::Get(espm::ActorValue av) const noexcept
{
 auto i=effects.find(av);if(i!=effects.end())return std::cref(i->second);
 auto j=stacked.find(av);if(j!=stacked.end()&&!j->second.empty())return std::cref(j->second.begin()->second);
 return std::nullopt;
}
std::optional<std::reference_wrapper<const ActiveMagicEffectsMap::Entry>>
ActiveMagicEffectsMap::Get(espm::ActorValue av,uint32_t id) const noexcept
{
 auto i=effects.find(av);if(i!=effects.end()&&i->second.data.effectId==id)return std::cref(i->second);
 auto j=stacked.find(av);if(j!=stacked.end())for(const auto& [key,e]:j->second)if(e.data.effectId==id)return std::cref(e);
 return std::nullopt;
}
void ActiveMagicEffectsMap::AddStacked(espm::ActorValue av,Entry entry)
{
 if(entry.provenance)ValidateProvenance(*entry.provenance);
 auto i=effects.find(av);if(i!=effects.end()){stacked[av][StackKey(i->second)]=i->second;effects.erase(i);}
 const auto key=StackKey(entry);stacked[av][key]=std::move(entry);
}
std::optional<std::reference_wrapper<const ActiveMagicEffectsMap::Entry>>
ActiveMagicEffectsMap::GetByStackKey(espm::ActorValue av,const std::string& key) const noexcept
{
 auto i=stacked.find(av);if(i!=stacked.end()){auto j=i->second.find(key);if(j!=i->second.end())return std::cref(j->second);}
 auto e=effects.find(av);if(e!=effects.end()&&StackKey(e->second)==key)return std::cref(e->second);return std::nullopt;
}
void ActiveMagicEffectsMap::RemoveByStackKey(espm::ActorValue av,const std::string& key) noexcept
{
 auto i=stacked.find(av);if(i!=stacked.end()){i->second.erase(key);if(i->second.empty())stacked.erase(i);}
 auto e=effects.find(av);if(e!=effects.end()&&StackKey(e->second)==key)effects.erase(e);
}
void ActiveMagicEffectsMap::Remove(espm::ActorValue av) noexcept{effects.erase(av);stacked.erase(av);}
void ActiveMagicEffectsMap::Remove(espm::ActorValue av,uint32_t id) noexcept
{
 auto i=effects.find(av);if(i!=effects.end()&&i->second.data.effectId==id)effects.erase(i);
 auto j=stacked.find(av);if(j!=stacked.end()){for(auto k=j->second.begin();k!=j->second.end();)if(k->second.data.effectId==id)k=j->second.erase(k);else ++k;if(j->second.empty())stacked.erase(j);}
}
void ActiveMagicEffectsMap::Clear() noexcept{effects.clear();stacked.clear();}
ActiveMagicEffectsMap ActiveMagicEffectsMap::FromJson(const simdjson::dom::element& value)
{
 ActiveMagicEffectsMap out;if(!value.is_array())return out;
 for(const auto row:value){
  if(!row.is_object())throw std::runtime_error("Invalid persisted magic effect");
  Entry e;std::string stamp;ReadEx(row,JsonPointer("endTime"),&stamp);
  int32_t av;ReadEx(row,JsonPointer("actorValue"),&av);ReadEx(row,JsonPointer("effectId"),&e.data.effectId);ReadEx(row,JsonPointer("duration"),&e.data.duration);ReadEx(row,JsonPointer("magnitude"),&e.data.magnitude);ReadEx(row,JsonPointer("areaOfEffect"),&e.data.areaOfEffect);
  auto provenance=row["srpProvenanceV1"];
  if(!provenance.error()){
    e.provenance=nlohmann::json::parse(std::string(simdjson::minify(provenance.value())));ValidateProvenance(*e.provenance);
    if(e.provenance->value("mgefConditionAtApply",std::string())!="ACTIVE"||!e.provenance->contains("mgefConditionFingerprint")||!e.provenance->at("mgefConditionFingerprint").is_string())throw std::runtime_error("SRP_EFFECT_CONDITION_EVIDENCE_CORRUPT");
    if(e.provenance->at("actorValue")!=av)throw std::runtime_error("SRP_EFFECT_ACTOR_VALUE_CORRUPT");
    const auto endMs=e.provenance->at("expiresAtMs").get<int64_t>();
    const int64_t expected=e.provenance->at("appliedAtMs").get<int64_t>()+int64_t(e.data.duration)*1000;
    if(endMs!=expected)throw std::runtime_error("SRP_EFFECT_EXPIRATION_CORRUPT");
    e.endTime=std::chrono::system_clock::time_point(std::chrono::milliseconds(endMs));
  }else e.endTime=Viet::TimeUtils::SystemTimeFrom(stamp);
  if(!std::isfinite(e.data.magnitude)||e.data.magnitude<0||e.data.magnitude>10000)throw std::runtime_error("SRP_EFFECT_MAGNITUDE_CORRUPT");
  if(std::chrono::system_clock::now()>e.endTime)continue;
  bool multiple=false;auto flag=row["srpStacked"];if(!flag.error())multiple=bool(flag.get_bool().value());
  if(e.provenance&&((e.provenance->at("stackPolicy")=="REPLACE_SOURCE_EFFECT_CASTER_TARGET")!=multiple))throw std::runtime_error("SRP_EFFECT_STACK_FORMAT_CORRUPT");
  if(e.provenance&&out.GetByStackKey(static_cast<espm::ActorValue>(av),StackKey(e)))throw std::runtime_error("SRP_EFFECT_DUPLICATE_STACK_KEY");
  if(multiple){if(out.GetByStackKey(static_cast<espm::ActorValue>(av),StackKey(e)))throw std::runtime_error("SRP_EFFECT_DUPLICATE_STACK_KEY");out.AddStacked(static_cast<espm::ActorValue>(av),std::move(e));}else out.Add(static_cast<espm::ActorValue>(av),std::move(e));
 }
 return out;
}
nlohmann::json::array_t ActiveMagicEffectsMap::ToJson() const
{
 auto out=nlohmann::json::array();auto append=[&](auto av,const Entry& e,bool multiple){auto row=nlohmann::json{{"effectId",e.data.effectId},{"endTime",Viet::TimeUtils::ToString(e.endTime)},{"magnitude",e.data.magnitude},{"duration",e.data.duration},{"areaOfEffect",e.data.areaOfEffect},{"actorValue",static_cast<int32_t>(av)}};if(multiple)row["srpStacked"]=true;if(e.provenance)row["srpProvenanceV1"]=*e.provenance;out.push_back(row);};
 for(const auto&[av,e]:effects)append(av,e,false);for(const auto&[av,entries]:stacked)for(const auto&[id,e]:entries)append(av,e,true);return out;
}
std::vector<espm::Effects::Effect> ActiveMagicEffectsMap::GetAllEffects() const noexcept
{
 std::vector<espm::Effects::Effect> out;for(const auto&[av,e]:effects)out.push_back(e.data);for(const auto&[av,entries]:stacked)for(const auto&[id,e]:entries)out.push_back(e.data);return out;
}
bool ActiveMagicEffectsMap::Has(espm::ActorValue av) const noexcept{return effects.count(av)||stacked.count(av);}
bool ActiveMagicEffectsMap::Empty() const noexcept{return effects.empty()&&stacked.empty();}
std::vector<ActiveMagicEffectsMap::Entry> ActiveMagicEffectsMap::GetEntries() const
{
 std::vector<Entry> out;for(const auto&[av,e]:effects)out.push_back(e);for(const auto&[av,entries]:stacked)for(const auto&[key,e]:entries)out.push_back(e);return out;
}
