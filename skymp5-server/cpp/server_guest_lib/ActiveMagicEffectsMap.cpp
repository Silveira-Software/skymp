#include "ActiveMagicEffectsMap.h"
#include "JsonUtils.h"
#include <TimeUtils.h>

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
 auto j=stacked.find(av);if(j!=stacked.end()){auto k=j->second.find(id);if(k!=j->second.end())return std::cref(k->second);}
 return std::nullopt;
}
void ActiveMagicEffectsMap::AddStacked(espm::ActorValue av,Entry entry)
{
 auto i=effects.find(av);if(i!=effects.end()){stacked[av][i->second.data.effectId]=i->second;effects.erase(i);}
 stacked[av][entry.data.effectId]=std::move(entry);
}
void ActiveMagicEffectsMap::Remove(espm::ActorValue av) noexcept{effects.erase(av);stacked.erase(av);}
void ActiveMagicEffectsMap::Remove(espm::ActorValue av,uint32_t id) noexcept
{
 auto i=effects.find(av);if(i!=effects.end()&&i->second.data.effectId==id)effects.erase(i);
 auto j=stacked.find(av);if(j!=stacked.end()){j->second.erase(id);if(j->second.empty())stacked.erase(j);}
}
void ActiveMagicEffectsMap::Clear() noexcept{effects.clear();stacked.clear();}
ActiveMagicEffectsMap ActiveMagicEffectsMap::FromJson(const simdjson::dom::element& value)
{
 ActiveMagicEffectsMap out;if(!value.is_array())return out;
 for(const auto row:value){
  if(!row.is_object())throw std::runtime_error("Invalid persisted magic effect");
  Entry e;std::string stamp;ReadEx(row,JsonPointer("endTime"),&stamp);e.endTime=Viet::TimeUtils::SystemTimeFrom(stamp);
  if(std::chrono::system_clock::now()>e.endTime)continue;
  int32_t av;ReadEx(row,JsonPointer("actorValue"),&av);ReadEx(row,JsonPointer("effectId"),&e.data.effectId);ReadEx(row,JsonPointer("duration"),&e.data.duration);ReadEx(row,JsonPointer("magnitude"),&e.data.magnitude);ReadEx(row,JsonPointer("areaOfEffect"),&e.data.areaOfEffect);
  bool multiple=false;auto flag=row["srpStacked"];if(!flag.error())multiple=bool(flag.get_bool().value());
  if(multiple)out.AddStacked(static_cast<espm::ActorValue>(av),std::move(e));else out.Add(static_cast<espm::ActorValue>(av),std::move(e));
 }
 return out;
}
nlohmann::json::array_t ActiveMagicEffectsMap::ToJson() const
{
 auto out=nlohmann::json::array();auto append=[&](auto av,const Entry& e,bool multiple){auto row=nlohmann::json{{"effectId",e.data.effectId},{"endTime",Viet::TimeUtils::ToString(e.endTime)},{"magnitude",e.data.magnitude},{"duration",e.data.duration},{"areaOfEffect",e.data.areaOfEffect},{"actorValue",static_cast<int32_t>(av)}};if(multiple)row["srpStacked"]=true;out.push_back(row);};
 for(const auto&[av,e]:effects)append(av,e,false);for(const auto&[av,entries]:stacked)for(const auto&[id,e]:entries)append(av,e,true);return out;
}
std::vector<espm::Effects::Effect> ActiveMagicEffectsMap::GetAllEffects() const noexcept
{
 std::vector<espm::Effects::Effect> out;for(const auto&[av,e]:effects)out.push_back(e.data);for(const auto&[av,entries]:stacked)for(const auto&[id,e]:entries)out.push_back(e.data);return out;
}
bool ActiveMagicEffectsMap::Has(espm::ActorValue av) const noexcept{return effects.count(av)||stacked.count(av);}
bool ActiveMagicEffectsMap::Empty() const noexcept{return effects.empty()&&stacked.empty();}
