#include "SrpActorModifiers.h"
#include "SrpActorStats.h"
#include "MpActor.h"
#include "WorldState.h"
#include "libespm/RecordHeaderAccess.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

bool SrpActorModifiers::Managed(const MpActor& actor)
{
  return actor.GetDynamicFields().GetAsJson().contains(SrpActorStats::kProperty);
}
bool SrpActorModifiers::Supported(espm::ActorValue av)
{
  using A=espm::ActorValue;
  return av==A::Health||av==A::Magicka||av==A::Stamina||av==A::CarryWeight||
    av==A::DamageResist||av==A::MagicResist;
}
namespace {
espm::MGEF::DATA EffectData(uint32_t id,WorldState* world)
{
  const auto record=world->GetEspm().GetBrowser().LookupById(id);
  if(!record.rec||record.rec->GetType()!=espm::MGEF::kType)throw std::runtime_error("SRP_MODIFIER_EFFECT_RECORD_MISSING");
  return espm::GetData<espm::MGEF>(id,world).data;
}
float Magnitude(const espm::Effects::Effect& effect, const espm::MGEF::DATA& data)
{
  if(!std::isfinite(effect.magnitude)||effect.magnitude<0||effect.magnitude>10000)
    throw std::runtime_error("SRP_MODIFIER_INVALID_MAGNITUDE");
  return data.IsFlagSet(espm::MGEF::Flags::Detrimental)?-effect.magnitude:effect.magnitude;
}
bool Peak(const espm::MGEF::DATA& data)
{
  return (data.effectType==espm::MGEF::EffectType::PeakValueMod||data.effectType==espm::MGEF::EffectType::ValueMod)&&data.IsFlagSet(espm::MGEF::Flags::Recover)&&
    SrpActorModifiers::Supported(data.primaryAV);
}
using Result=SrpActorModifiers::ConditionResult;
struct ConditionList { std::vector<espm::CTDA> values; bool valid=true; };
ConditionList Conditions(const espm::RecordHeader* rec,WorldState* world,
                         int effectIndex=-1)
{
  ConditionList out; int index=-1;
  espm::RecordHeaderAccess::IterateFields(rec,[&](const char* type,uint32_t size,const char* data){
    if(!memcmp(type,"EFID",4))++index;
    if(memcmp(type,"CTDA",4))return;
    // SPEL/ENCH CTDA belongs to one EFID, not to every effect in the record.
    if(effectIndex>=0&&index!=effectIndex){if(index<0)out.valid=false;return;}
    if(size!=sizeof(espm::CTDA)){out.valid=false;return;}
    espm::CTDA c{};memcpy(&c,data,sizeof(c));out.values.push_back(c);
  },world->GetEspmCache());
  return out;
}
Result TestConditions(const MpActor& actor,const espm::LookupResult& owner,
                      int effectIndex=-1)
{
  const auto list=Conditions(owner.rec,actor.GetParent(),effectIndex);
  return list.valid?SrpActorModifiers::EvaluateConditions(actor,owner,list.values):Result::Unknown;
}
Result EffectConditions(const MpActor& actor,uint32_t id)
{
  const auto record=actor.GetParent()->GetEspm().GetBrowser().LookupById(id);
  return record.rec?TestConditions(actor,record):Result::Unknown;
}
bool Include(Result a,Result b,bool& complete)
{
  if(a==Result::Unknown||b==Result::Unknown){complete=false;return false;}
  return a==Result::Active&&b==Result::Active;
}
}
SrpActorModifiers::ConditionResult SrpActorModifiers::EvaluateConditions(
  const MpActor& actor,const espm::LookupResult& owner,
  const std::vector<espm::CTDA>& conditions)
{
  if(conditions.empty())return Result::Active;
  auto world=actor.GetParent();if(!world||!world->HasEspm()||!owner.rec)return Result::Unknown;
  bool all=true,group=false;
  for(size_t i=0;i<conditions.size();++i){
    const auto& c=conditions[i];const auto flags=static_cast<uint8_t>(c.GetFlags());
    // Only the authoritative subject is available here. Caster/target, global
    // variables, quest aliases and swapped/packed arguments require context
    // absent from the current durable active-effect format. Never guess.
    if((flags&~1u)||c.runOnType!=espm::CTDA::RunOnTypeFlags::Subject||
       c.reference!=0||!std::isfinite(c.comparisonValue))return Result::Unknown;
    const auto args=c.GetDefaultData();float value=0;
    if(args.secondParameter)return Result::Unknown;
    switch(c.functionIndex){
      case 47: case 69: {
        const auto id=owner.ToGlobalId(args.firstParameter);
        if(!id||!world->GetEspm().GetBrowser().LookupById(id).rec)return Result::Unknown;
        value=c.functionIndex==47?actor.GetInventory().GetItemCount(id):actor.GetRaceId()==id;
        break;
      }
      case 300: {
        if(args.firstParameter)return Result::Unknown;
        const auto cell=actor.GetCellOrWorld().ToFormId(world->espmFiles);
        const auto rec=world->GetEspm().GetBrowser().LookupById(cell);
        if(!rec.rec)return Result::Unknown;
        value=rec.rec->GetType()==espm::CELL::kType;break;
      }
      default:return Result::Unknown;
    }
    bool pass=false;
    switch(c.GetOperator()){
      case espm::CTDA::Operator::EqualTo:pass=value==c.comparisonValue;break;
      case espm::CTDA::Operator::NotEqualTo:pass=value!=c.comparisonValue;break;
      case espm::CTDA::Operator::GreaterThen:pass=value>c.comparisonValue;break;
      case espm::CTDA::Operator::GreaterThenOrEqualTo:pass=value>=c.comparisonValue;break;
      case espm::CTDA::Operator::LessThen:pass=value<c.comparisonValue;break;
      case espm::CTDA::Operator::LessThenOrEqualTo:pass=value<=c.comparisonValue;break;
      default:return Result::Unknown;
    }
    group=group||pass;
    if(!(flags&1)||i+1==conditions.size()){all=all&&group;group=false;}
  }
  return all?Result::Active:Result::Inactive;
}
SrpActorModifiers SrpActorModifiers::Read(const MpActor& actor,bool preparingProjection)
{
  SrpActorModifiers out;
  auto world=actor.GetParent();
  if((!Managed(actor)&&!preparingProjection)||!world||!world->HasEspm())return out;
  auto learned=actor.GetSpellList();std::set<uint32_t> spells(learned.begin(),learned.end());
  const auto npc=world->GetEspm().GetBrowser().LookupById(actor.GetBaseId());
  if(npc.rec&&npc.rec->GetType()==espm::NPC_::kType)for(auto id:espm::GetData<espm::NPC_>(actor.GetBaseId(),world).spells)spells.insert(npc.ToGlobalId(id));
  const auto raceId=actor.GetRaceId();const auto race=world->GetEspm().GetBrowser().LookupById(raceId);
  if(race.rec&&race.rec->GetType()==espm::RACE::kType)for(auto id:espm::GetData<espm::RACE>(raceId,world).spells)spells.insert(race.ToGlobalId(id));
  for(auto spell:spells){
    const auto owner=world->GetEspm().GetBrowser().LookupById(spell);
    if(!owner.rec||owner.rec->GetType()!=espm::SPEL::kType)continue;
    const auto record=espm::GetData<espm::SPEL>(spell,world);
    if(!record.spellItem||(record.spellItem->type!=espm::SPEL::SpellType::Ability&&record.spellItem->type!=espm::SPEL::SpellType::Disease))continue;
    int effectIndex=-1;
    for(const auto& e:record.effects){
      ++effectIndex;
      if(!e.effectItem)continue;
      espm::Effects::Effect effect{owner.ToGlobalId(e.effectFormId),e.effectItem->magnitude,0,0};
      const auto data=EffectData(effect.effectId,world);
      if(Peak(data)&&Include(TestConditions(actor,owner,effectIndex),EffectConditions(actor,effect.effectId),out.complete))out.passive[data.primaryAV]+=Magnitude(effect,data);
    }
  }
  // Live effects expire through the world timer with resource reconciliation.
  for(const auto& effect:actor.GetActiveMagicEffects().GetAllEffects()){
    const auto data=EffectData(effect.effectId,world);
    if(Peak(data)&&Include(Result::Active,EffectConditions(actor,effect.effectId),out.complete))out.temporary[data.primaryAV]+=Magnitude(effect,data);
  }
  std::set<uint32_t> seen;uint32_t occupied=0;
  for(const auto& entry:actor.GetEquipment().inv.entries){
    if(entry.GetWorn()==Inventory::Worn::None||!seen.insert(entry.baseId).second||
       !actor.GetInventory().HasItem(entry.baseId))continue;
    const auto record=world->GetEspm().GetBrowser().LookupById(entry.baseId);
    if(!record.rec||record.rec->GetType()!=espm::ARMO::kType)continue;
    const auto armor=espm::GetData<espm::ARMO>(entry.baseId,world);
    const uint32_t slots=armor.bod2.present?armor.bod2.bodyPartFlags:armor.bodt.present?armor.bodt.bodyPartFlags:0;
    if(!slots||(occupied&slots))continue;
    occupied|=slots;
    // Read enchantment identity from the authoritative inventory, never the
    // client equipment payload. Worn bits differ between these two views.
    auto actual=std::find_if(actor.GetInventory().entries.begin(),actor.GetInventory().entries.end(),[&](const auto& v){auto a=v,b=entry;a.SetWorn(Inventory::Worn::None);b.SetWorn(Inventory::Worn::None);return a.EqualExceptCount(b);});
    if(actual==actor.GetInventory().entries.end())continue;
    uint32_t enchantment=actual->enchantmentId.value_or(0);
    if(!enchantment&&armor.enchantmentFormId)enchantment=record.ToGlobalId(armor.enchantmentFormId);
    if(!enchantment)continue;
    const auto owner=world->GetEspm().GetBrowser().LookupById(enchantment);
    if(!owner.rec||owner.rec->GetType()!=espm::ENCH::kType){out.complete=false;continue;}
    int effectIndex=-1;
    for(auto effect:espm::GetData<espm::ENCH>(enchantment,world).effects){
      ++effectIndex;
      effect.effectId=owner.ToGlobalId(effect.effectId);
      const auto data=EffectData(effect.effectId,world);
      if(Peak(data)&&Include(TestConditions(actor,owner,effectIndex),EffectConditions(actor,effect.effectId),out.complete))out.equipment[data.primaryAV]+=Magnitude(effect,data);
    }
  }
  return out;
}
float SrpActorModifiers::Sum(espm::ActorValue av) const
{
  auto get=[&](const auto& map){auto i=map.find(av);return i==map.end()?0.f:i->second;};
  return std::clamp(get(equipment)+get(temporary)+get(passive),-10000.f,10000.f);
}
BaseActorValues SrpActorModifiers::Compose(BaseActorValues permanent) const
{
  permanent.health=std::clamp(permanent.health+Sum(espm::ActorValue::Health),1.f,100000.f);
  permanent.magicka=std::clamp(permanent.magicka+Sum(espm::ActorValue::Magicka),0.f,100000.f);
  permanent.stamina=std::clamp(permanent.stamina+Sum(espm::ActorValue::Stamina),1.f,100000.f);
  return permanent;
}
bool SrpActorModifiers::Apply(MpActor& actor,espm::Effects::Effect& effect,bool restoring)
{
  if(!Managed(actor))return false;
  auto world=actor.GetParent();const auto data=EffectData(effect.effectId,world);
  if(!Peak(data))return false;
  const auto condition=EffectConditions(actor,effect.effectId);
  if(condition==Result::Unknown)throw std::runtime_error("SRP_MODIFIER_CONDITION_REVIEW_REQUIRED");
  if(condition==Result::Inactive)return true;
  Magnitude(effect,data);
  auto end=std::chrono::system_clock::now()+std::chrono::seconds(effect.duration);
  auto prior=actor.GetActiveMagicEffects().Get(data.primaryAV,effect.effectId);
  if(restoring){if(!prior)return true;end=prior->get().endTime;}
  const auto old=actor.GetActorValues();
  if(prior)world->RemoveEffectTimer(prior->get().timerId);
  uint32_t timer;const auto id=actor.GetFormId();std::weak_ptr<MpForm> identity=world->LookupFormByIdNoLoad(id);
  world->SetEffectTimer(std::chrono::duration_cast<std::chrono::milliseconds>(end-std::chrono::system_clock::now()),&timer).Then([world,id,identity,end,effectId=effect.effectId,av=data.primaryAV](Viet::Void){auto target=identity.lock(),current=world->LookupFormByIdNoLoad(id);if(!target||current.get()!=target.get()||!target->AsActor())return;const auto active=target->AsActor()->GetActiveMagicEffects().Get(av,effectId);if(active&&active->get().endTime==end&&active->get().data.effectId==effectId){auto actor=target->AsActor();const auto before=actor->GetActorValues();actor->EditChangeForm([&](MpChangeForm& form){form.activeMagicEffects.Remove(av,effectId);});actor->ReconcileSrpMaximums(before);}});
  actor.EditChangeForm([&](MpChangeForm& form){form.activeMagicEffects.AddStacked(data.primaryAV,ActiveMagicEffectsMap::Entry{timer,effect,end});});
  actor.ReconcileSrpMaximums(old);
  return true;
}
