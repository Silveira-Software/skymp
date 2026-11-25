#include "SrpActorModifiers.h"
#include "SrpActorStats.h"
#include "MpActor.h"
#include "WorldState.h"
#include "libespm/RecordHeaderAccess.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
#include <random>
#include <sstream>
#include <iomanip>
#include <cctype>

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
std::string NewIdentity()
{
 std::random_device random;std::ostringstream s;s<<std::hex;
 for(int i=0;i<4;++i)s<<std::setw(8)<<std::setfill('0')<<random();return s.str();
}
nlohmann::json ActorIdentity(const MpActor& actor)
{
 const auto& fields=actor.GetDynamicFields().GetAsJson();
 return {{"reference",FormDesc::FromFormId(actor.GetFormId(),actor.GetParent()->espmFiles).ToString()},
 {"base",FormDesc::FromFormId(actor.GetBaseId(),actor.GetParent()->espmFiles).ToString()},
 {"profileId",std::to_string(actor.GetProfileId())},{"characterId",fields.value("srpActiveCharacterId",std::string())},
 {"incarnation",fields.value("private.srpEffectActorIdentityV1",std::string())}};
}
void EnsureIdentity(MpActor& actor)
{
 if(ActorIdentity(actor).at("incarnation")=="")actor.SetPropertyValueDump("private.srpEffectActorIdentityV1",nlohmann::json(NewIdentity()).dump(),false,false);
}
std::vector<espm::Effects::Effect> SourceEffects(uint32_t source,WorldState* world)
{
 const auto owner=world->GetEspm().GetBrowser().LookupById(source);
 if(!owner.rec)throw std::runtime_error("SRP_EFFECT_SOURCE_MISSING");
 std::vector<espm::Effects::Effect> effects;
 if(owner.rec->GetType()==espm::ALCH::kType)effects=espm::GetData<espm::ALCH>(source,world).effects;
 else if(owner.rec->GetType()==espm::ENCH::kType)effects=espm::GetData<espm::ENCH>(source,world).effects;
 else if(owner.rec->GetType()==espm::SPEL::kType){for(const auto& e:espm::GetData<espm::SPEL>(source,world).effects){if(!e.effectItem)throw std::runtime_error("SRP_EFFECT_SOURCE_EFIT_MISSING");effects.push_back({e.effectFormId,e.effectItem->magnitude,e.effectItem->areaOfEffect,e.effectItem->duration});}}
 else throw std::runtime_error("SRP_EFFECT_SOURCE_TYPE_UNSUPPORTED");
 for(auto& effect:effects)effect.effectId=owner.ToGlobalId(effect.effectId);return effects;
}
const MpActor* ResolveCaster(const nlohmann::json& p,WorldState* world)
{
 const auto id=FormDesc::FromString(p.at("caster").at("reference").get<std::string>()).ToFormId(world->espmFiles);
 const auto form=world->LookupFormByIdNoLoad(id);const auto caster=form?form->AsActor():nullptr;
 return caster&&ActorIdentity(*caster)==p.at("caster")?caster:nullptr;
}
uint32_t ValidateSource(const nlohmann::json& p,const MpActor& target,const espm::Effects::Effect& effect)
{
 ActiveMagicEffectsMap::ValidateProvenance(p);auto world=target.GetParent();
 if(ActorIdentity(target)!=p.at("target"))throw std::runtime_error("SRP_EFFECT_TARGET_IDENTITY_MISMATCH");
 const auto source=FormDesc::FromString(p.at("source").get<std::string>()).ToFormId(world->espmFiles);
 const auto owner=world->GetEspm().GetBrowser().LookupById(source);
 if(!owner.rec||owner.rec->GetType().ToString()!=p.at("sourceType").get<std::string>()||world->espmFiles.at(owner.fileIdx)!=p.at("recordOwner").get<std::string>())throw std::runtime_error("SRP_EFFECT_SOURCE_RECORD_MISMATCH");
 const auto effects=SourceEffects(source,world);const size_t index=p.at("effectIndex").get<uint32_t>();
 const auto mgef=FormDesc::FromString(p.at("mgef").get<std::string>()).ToFormId(world->espmFiles);
 if(index>=effects.size()||effects[index].effectId!=mgef||mgef!=effect.effectId||effects[index].magnitude!=effect.magnitude||effects[index].duration!=effect.duration||effects[index].areaOfEffect!=effect.areaOfEffect||p.at("actorValue")!=static_cast<int32_t>(EffectData(mgef,world).primaryAV))throw std::runtime_error("SRP_EFFECT_SOURCE_PAYLOAD_MISMATCH");
 return source;
}
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
std::string ConditionFingerprint(uint32_t id,WorldState* world)
{
 const auto record=world->GetEspm().GetBrowser().LookupById(id);if(!record.rec)return "MISSING";
 const auto list=Conditions(record.rec,world);if(!list.valid)return "MALFORMED";
 const char hex[]="0123456789abcdef";std::string value;
 for(const auto& c:list.values){const auto bytes=reinterpret_cast<const uint8_t*>(&c);for(size_t i=0;i<sizeof(c);++i){value+=hex[bytes[i]>>4];value+=hex[bytes[i]&15];}}
 return value;
}
Result TestConditions(const MpActor& actor,const espm::LookupResult& owner,
                      int effectIndex=-1,const MpActor* caster=nullptr)
{
  const auto list=Conditions(owner.rec,actor.GetParent(),effectIndex);
  return list.valid?SrpActorModifiers::EvaluateConditions(actor,owner,list.values,caster):Result::Unknown;
}
Result EffectConditions(const MpActor& actor,uint32_t id,const MpActor* caster=nullptr)
{
  const auto record=actor.GetParent()->GetEspm().GetBrowser().LookupById(id);
  return record.rec?TestConditions(actor,record,-1,caster):Result::Unknown;
}
bool Include(Result a,Result b,bool& complete)
{
  if(a==Result::Unknown||b==Result::Unknown){complete=false;return false;}
  return a==Result::Active&&b==Result::Active;
}
}
SrpActorModifiers::ConditionResult SrpActorModifiers::EvaluateConditions(
  const MpActor& actor,const espm::LookupResult& owner,
  const std::vector<espm::CTDA>& conditions,const MpActor* caster)
{
  if(conditions.empty())return Result::Active;
  auto world=actor.GetParent();if(!world||!world->HasEspm()||!owner.rec)return Result::Unknown;
  bool all=true,group=false;
  for(size_t i=0;i<conditions.size();++i){
    const auto& c=conditions[i];const auto flags=static_cast<uint8_t>(c.GetFlags());
    // Only the authoritative subject is available here. Caster/target, global
    // variables, quest aliases and swapped/packed arguments require context
    // absent from the current durable active-effect format. Never guess.
    if((flags&~1u)||!std::isfinite(c.comparisonValue))return Result::Unknown;
    const MpActor* subject=&actor;
    if(c.runOnType==espm::CTDA::RunOnTypeFlags::Reference){
      const auto ref=owner.ToGlobalId(static_cast<uint32_t>(c.reference));
      if(ref==actor.GetFormId())subject=&actor;
      else if(caster&&ref==caster->GetFormId())subject=caster;
      else return Result::Unknown;
    }else if(c.runOnType!=espm::CTDA::RunOnTypeFlags::Subject||c.reference!=0)return Result::Unknown;
    const auto args=c.GetDefaultData();float value=0;
    if(args.secondParameter)return Result::Unknown;
    switch(c.functionIndex){
      case 47: case 69: case 72: case 182: case 264: case 560: case 682: case 722: {
        const auto id=owner.ToGlobalId(args.firstParameter);
        if(!id||!world->GetEspm().GetBrowser().LookupById(id).rec)return Result::Unknown;
        const auto parameter=world->GetEspm().GetBrowser().LookupById(id).rec;
        if((c.functionIndex==69&&parameter->GetType()!=espm::RACE::kType)||(c.functionIndex==264&&parameter->GetType()!=espm::SPEL::kType))return Result::Unknown;
        if(c.functionIndex==47)value=subject->GetInventory().GetItemCount(id);
        else if(c.functionIndex==69)value=subject->GetRaceId()==id;
        else if(c.functionIndex==72)value=subject->GetBaseId()==id;
        else if(c.functionIndex==264){
          const auto learned=subject->GetSpellList();value=std::find(learned.begin(),learned.end(),id)!=learned.end();
          for(auto source:{subject->GetBaseId(),subject->GetRaceId()}){const auto rec=world->GetEspm().GetBrowser().LookupById(source);if(!rec.rec)return Result::Unknown;
            std::set<uint32_t> list;if(rec.rec->GetType()==espm::NPC_::kType)list=espm::GetData<espm::NPC_>(source,world).spells;else if(rec.rec->GetType()==espm::RACE::kType)list=espm::GetData<espm::RACE>(source,world).spells;
            for(auto spell:list)if(rec.ToGlobalId(spell)==id)value=1;
          }
        }else if(c.functionIndex==560){
          if(parameter->GetType()!=espm::KYWD::kType)return Result::Unknown;
          for(const auto source:{subject->GetBaseId(),subject->GetRaceId()}){
            const auto rec=world->GetEspm().GetBrowser().LookupById(source);if(!rec.rec)return Result::Unknown;
            for(const auto key:rec.rec->GetKeywordIds(world->GetEspmCache()))if(rec.ToGlobalId(key)==id)value=1;
          }
        }else{
          const auto keyword=world->GetEspm().GetBrowser().LookupById(id);
          if((c.functionIndex==682||c.functionIndex==722)&&keyword.rec->GetType()!=espm::KYWD::kType)return Result::Unknown;
          std::set<std::string> counted;
          for(const auto& worn:subject->GetEquipment().inv.entries){
            if(worn.GetWorn()==Inventory::Worn::None)continue;
            const auto actual=std::find_if(subject->GetInventory().entries.begin(),subject->GetInventory().entries.end(),[&](auto a){auto b=worn;a.SetWorn(Inventory::Worn::None);b.SetWorn(Inventory::Worn::None);return a.count>0&&a.EqualExceptCount(b);});
            if(actual==subject->GetInventory().entries.end())continue;
            if(c.functionIndex==182){if(worn.baseId==id)value=1;continue;}
            const auto rec=world->GetEspm().GetBrowser().LookupById(worn.baseId);if(!rec.rec)return Result::Unknown;
            if(c.functionIndex==722){
              if(rec.rec->GetType()!=espm::ARMO::kType)continue;
              const auto armor=espm::GetData<espm::ARMO>(worn.baseId,world);
              const auto slots=armor.bod2.present?armor.bod2.bodyPartFlags:armor.bodt.present?armor.bodt.bodyPartFlags:0;
              if(!(slots&(1u|2u|4u|8u|128u|4096u)))continue;
              Inventory identity;auto entry=worn;entry.count=1;entry.SetWorn(Inventory::Worn::None);identity.entries.push_back(entry);
              if(!counted.insert(identity.ToJson().dump()).second)continue;
            }
            for(auto key:rec.rec->GetKeywordIds(world->GetEspmCache()))if(rec.ToGlobalId(key)==id){if(c.functionIndex==722)++value;else value=1;break;}
          }
        }
        break;
      }
      case 80: {
        if (args.firstParameter) return Result::Unknown;
        // Only a current server projection proves the player's level. NPC
        // auto-calc/template levels and the client vanilla level are not inputs.
        const auto fields = subject->GetDynamicFields().GetAsJson();
        if (!fields.contains("srpProgression"))
          return Result::Unknown;
        try {
          const auto canonical = SrpActorStats::FromProjection(fields.at("srpProgression"));
          if (!canonical.revision || canonical.profileId != std::to_string(subject->GetProfileId()) ||
            !fields.contains("srpActiveCharacterId") || fields.at("srpActiveCharacterId") != canonical.characterId)
            return Result::Unknown;
          // The canonical snapshot may be one revision AHEAD while the same
          // native ApplySrpActorStats call is composing modifiers. Requiring
          // equal projections here would deadlock a legitimate level update.
          if (SrpActorStats::Read(*subject).revision > canonical.revision) return Result::Unknown;
          value = canonical.level;
        } catch (const std::exception&) { return Result::Unknown; }
        break;
      }
      case 46: {
        if(args.firstParameter)return Result::Unknown;value=subject->IsDead();break;
      }
      case 71: {
        const auto id=owner.ToGlobalId(args.firstParameter);const auto rec=world->GetEspm().GetBrowser().LookupById(id);
        if(!rec.rec||rec.rec->GetType()!=espm::FACT::kType)return Result::Unknown;
        const auto form=subject->GetChangeForm();if(!form.factions)return Result::Unknown;
        for(const auto& faction:*form.factions)if(faction.formDesc.ToFormId(world->espmFiles)==id&&faction.rank>=0)value=1;
        break;
      }
      case 310: {
        const auto id=owner.ToGlobalId(args.firstParameter);const auto rec=world->GetEspm().GetBrowser().LookupById(id);
        if(!rec.rec||rec.rec->GetType()!=espm::WRLD::kType)return Result::Unknown;
        const auto location=subject->GetCellOrWorld().ToFormId(world->espmFiles);const auto current=world->GetEspm().GetBrowser().LookupById(location);
        if(!current.rec||current.rec->GetType()!=espm::WRLD::kType)return Result::Unknown;
        value=id==location;break;
      }
      case 640: {
        const auto& av=subject->GetActorValues();
        switch(static_cast<espm::ActorValue>(args.firstParameter)){
          case espm::ActorValue::Health:value=av.healthPercentage;break;
          case espm::ActorValue::Magicka:value=av.magickaPercentage;break;
          case espm::ActorValue::Stamina:value=av.staminaPercentage;break;
          default:return Result::Unknown;
        }
        if(!std::isfinite(value)||value<0||value>1)return Result::Unknown;break;
      }
      case 448: {
        const auto id=owner.ToGlobalId(args.firstParameter);const auto rec=world->GetEspm().GetBrowser().LookupById(id);
        if(!rec.rec||rec.rec->GetType().ToString()!="PERK")return Result::Unknown;
        const auto& fields=subject->GetDynamicFields().GetAsJson();if(!fields.contains("private.srpPerkAuthorityV1"))return Result::Unknown;
        const auto& ledger=fields.at("private.srpPerkAuthorityV1");const auto identity=ActorIdentity(*subject);
        if(!ledger.is_object()||ledger.value("schemaVersion",0)!=1||ledger.value("authority",std::string())!="SRP-PERKS-V1"||!ledger.value("complete",false)||ledger.value("profileId",std::string())!=identity.at("profileId").get<std::string>()||ledger.value("characterId",std::string())!=identity.at("characterId").get<std::string>()||!ledger.contains("revision")||!ledger.at("revision").is_number_unsigned()||!ledger.at("revision").get<uint64_t>()||!ledger.contains("perks")||!ledger.at("perks").is_array())return Result::Unknown;
        std::set<std::string> seen;
        for(const auto& perk:ledger.at("perks")){
          if(!perk.is_string()||!seen.insert(perk.get<std::string>()).second)return Result::Unknown;
          uint32_t perkId;try{perkId=FormDesc::FromString(perk.get<std::string>()).ToFormId(world->espmFiles);}catch(...){return Result::Unknown;}
          const auto record=world->GetEspm().GetBrowser().LookupById(perkId);
          if(!record.rec||record.rec->GetType().ToString()!="PERK")return Result::Unknown;
          if(perkId==id)value=1;
        }
        break;
      }
      case 300: {
        if(args.firstParameter)return Result::Unknown;
        const auto cell=subject->GetCellOrWorld().ToFormId(world->espmFiles);
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
nlohmann::json SrpActorModifiers::CreateProvenance(MpActor& target,uint32_t source,
  uint32_t index,MpActor& caster,const std::string& operationId)
{
 auto world=target.GetParent();if(!world||world!=caster.GetParent())throw std::runtime_error("SRP_EFFECT_WORLD_MISMATCH");
 if(operationId.size()>128)throw std::runtime_error("SRP_EFFECT_OPERATION_ID_INVALID");
 const auto effects=SourceEffects(source,world);if(index>=effects.size())throw std::runtime_error("SRP_EFFECT_INDEX_INVALID");
 const auto owner=world->GetEspm().GetBrowser().LookupById(source);EnsureIdentity(target);EnsureIdentity(caster);
 nlohmann::json p={{"schemaVersion",1},{"mgef",FormDesc::FromFormId(effects[index].effectId,world->espmFiles).ToString()},
 {"source",FormDesc::FromFormId(source,world->espmFiles).ToString()},{"recordOwner",world->espmFiles.at(owner.fileIdx)},
 {"sourceType",owner.rec->GetType().ToString()},{"effectIndex",index},{"caster",ActorIdentity(caster)},{"target",ActorIdentity(target)},
 {"instanceId",NewIdentity()},{"operationId",operationId},{"actorValue",static_cast<int32_t>(EffectData(effects[index].effectId,world).primaryAV)},
 {"overlapPolicy","STRONGEST_ABSOLUTE_SAME_MGEF_TARGET"},
 {"stackPolicy",Peak(EffectData(effects[index].effectId,world))?"REPLACE_SOURCE_EFFECT_CASTER_TARGET":"LEGACY_REPLACE_ACTOR_VALUE"},
 {"appliedAtMs",std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()}};
 p["stackKey"]=p.at("stackPolicy")=="LEGACY_REPLACE_ACTOR_VALUE"?"legacy-av:"+std::to_string(p.at("actorValue").get<int>()):nlohmann::json::array({p.at("sourceType"),p.at("source"),p.at("effectIndex"),p.at("caster"),p.at("target")}).dump();
 p["mgefConditionFingerprint"]=ConditionFingerprint(effects[index].effectId,world);
 p["expiresAtMs"]=p.at("appliedAtMs").get<int64_t>()+int64_t(effects[index].duration)*1000;
 const auto initial=EffectConditions(target,effects[index].effectId,&caster);
 p["mgefConditionAtApply"]=initial==Result::Active?"ACTIVE":initial==Result::Inactive?"INACTIVE":"UNKNOWN";
 ActiveMagicEffectsMap::ValidateProvenance(p);return p;
}
void SrpActorModifiers::ApplyFromSource(MpActor& target,uint32_t source,MpActor& caster,
  const std::string& operationId,bool hasSweetpie)
{
 // Internal native interface. No new packet or client-supplied caster/origin.
 auto effects=SourceEffects(source,target.GetParent());
 for(uint32_t i=0;i<effects.size();++i){auto provenance=CreateProvenance(target,source,i,caster,operationId);target.ApplyMagicEffect(effects[i],hasSweetpie,false,&provenance);}
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
  std::map<uint32_t,std::pair<espm::ActorValue,float>> activeByMgef;
  for(const auto& entry:actor.GetActiveMagicEffects().GetEntries()){
    auto effect=entry.data;
    if(entry.provenance)effect.effectId=FormDesc::FromString(entry.provenance->at("mgef").get<std::string>()).ToFormId(world->espmFiles);
    const auto data=EffectData(effect.effectId,world);if(!Peak(data))continue;
    Result sourceCondition=Result::Active,mgefCondition=Result::Unknown;
    if(entry.provenance){
      if(ActorIdentity(actor)!=entry.provenance->at("target")){out.complete=false;continue;}
      effect.effectId=FormDesc::FromString(entry.provenance->at("mgef").get<std::string>()).ToFormId(world->espmFiles);
      const auto source=ValidateSource(*entry.provenance,actor,effect);
      const auto caster=ResolveCaster(*entry.provenance,world);
      sourceCondition=TestConditions(actor,world->GetEspm().GetBrowser().LookupById(source),entry.provenance->at("effectIndex").get<int>(),caster);
      mgefCondition=entry.provenance->value("mgefConditionAtApply",std::string())=="ACTIVE"&&entry.provenance->value("mgefConditionFingerprint",std::string("MISSING"))==ConditionFingerprint(effect.effectId,world)?Result::Active:Result::Unknown;
    }else{
      // Preserve old known-unconditional resource modifiers, but do not claim
      // complete provenance or authorize new point allocation on this actor.
      out.complete=false;mgefCondition=EffectConditions(actor,effect.effectId);
    }
    if(Peak(data)&&Include(sourceCondition,mgefCondition,out.complete)){
      const auto magnitude=Magnitude(effect,data);auto found=activeByMgef.find(effect.effectId);
      if(found==activeByMgef.end()||std::abs(magnitude)>std::abs(found->second.second))activeByMgef[effect.effectId]={data.primaryAV,magnitude};
    }
  }
  // Preserve independently expiring/caster-scoped rows without multiplying a
  // shared MGEF bonus by the number of casters or potion-source variants.
  for(const auto& [id,value]:activeByMgef)out.temporary[value.first]+=value.second;
  std::set<uint32_t> seen;uint32_t occupied=0;
  for(const auto& entry:actor.GetEquipment().inv.entries){
    if(entry.GetWorn()==Inventory::Worn::None||!seen.insert(entry.baseId).second||
       !actor.GetInventory().HasItem(entry.baseId))continue;
    const auto record=world->GetEspm().GetBrowser().LookupById(entry.baseId);
    if(!record.rec||record.rec->GetType()!=espm::ARMO::kType)continue;
    const auto armor=espm::GetData<espm::ARMO>(entry.baseId,world);
    const uint32_t slots=armor.bod2.present?armor.bod2.bodyPartFlags:armor.bodt.present?armor.bodt.bodyPartFlags:0;
    if(!slots||(occupied&slots))continue;
    // Read enchantment identity from the authoritative inventory, never the
    // client equipment payload. Worn bits differ between these two views.
    auto actual=std::find_if(actor.GetInventory().entries.begin(),actor.GetInventory().entries.end(),[&](const auto& v){auto a=v,b=entry;a.SetWorn(Inventory::Worn::None);b.SetWorn(Inventory::Worn::None);return a.count>0&&a.EqualExceptCount(b);});
    if(actual==actor.GetInventory().entries.end()){out.complete=false;continue;}
    occupied|=slots;
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
      if(!Peak(data))continue;
      const auto sourceCondition=TestConditions(actor,owner,effectIndex),mgefCondition=EffectConditions(actor,effect.effectId);
      const bool active=Include(sourceCondition,mgefCondition,out.complete);
      Inventory item;auto canonical=*actual;canonical.count=1;canonical.SetWorn(Inventory::Worn::None);item.entries.push_back(canonical);
      const auto source=FormDesc::FromFormId(enchantment,world->espmFiles).ToString();
      const auto key=nlohmann::json::array({ActorIdentity(actor),item.ToJson(),slots,source,effectIndex}).dump();
      out.equipmentEffects.push_back({{"key",key},{"schemaVersion",1},{"sourceType","ENCH"},{"source",source},{"recordOwner",world->espmFiles.at(owner.fileIdx)},{"mgef",FormDesc::FromFormId(effect.effectId,world->espmFiles).ToString()},{"effectIndex",effectIndex},{"item",item.ToJson()},{"slotMask",slots},{"owner",ActorIdentity(actor)},{"magnitude",Magnitude(effect,data)},{"durationMode","WHILE_EQUIPPED"},{"state",active?"ACTIVE":sourceCondition==Result::Unknown||mgefCondition==Result::Unknown?"UNKNOWN":"INACTIVE"}});
      if(active)out.equipment[data.primaryAV]+=Magnitude(effect,data);
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
bool SrpActorModifiers::Apply(MpActor& actor,espm::Effects::Effect& effect,bool restoring,const nlohmann::json* provenance)
{
  if(!Managed(actor))return false;
  auto world=actor.GetParent();const auto data=EffectData(effect.effectId,world);
  auto savedOrigin=provenance?std::optional<nlohmann::json>(*provenance):std::nullopt;
  const MpActor* caster=nullptr;
  if(savedOrigin){const auto source=ValidateSource(*savedOrigin,actor,effect);caster=ResolveCaster(*savedOrigin,world);
    const auto sourceCondition=TestConditions(actor,world->GetEspm().GetBrowser().LookupById(source),savedOrigin->at("effectIndex").get<int>(),caster);
    if(sourceCondition==Result::Unknown)throw std::runtime_error("SRP_MODIFIER_SOURCE_CONDITION_REVIEW_REQUIRED");
    if(sourceCondition==Result::Inactive&&!restoring)return true;
  }
  if(restoring&&savedOrigin&&savedOrigin->value("mgefConditionFingerprint",std::string("MISSING"))!=ConditionFingerprint(effect.effectId,world))throw std::runtime_error("SRP_EFFECT_CONDITIONS_CHANGED_REQUIRES_REVIEW");
  const auto condition=restoring&&savedOrigin&&savedOrigin->value("mgefConditionAtApply",std::string())=="ACTIVE"?Result::Active:EffectConditions(actor,effect.effectId,caster);
  if(condition==Result::Unknown)throw std::runtime_error("SRP_MODIFIER_CONDITION_REVIEW_REQUIRED");
  if(condition==Result::Inactive)return true;
  if(!Peak(data))return false;
  Magnitude(effect,data);
  if(savedOrigin){(*savedOrigin)["mgefConditionAtApply"]="ACTIVE";(*savedOrigin)["mgefConditionFingerprint"]=ConditionFingerprint(effect.effectId,world);}
  auto end=std::chrono::system_clock::now()+std::chrono::seconds(effect.duration);
  if(savedOrigin)end=std::chrono::system_clock::time_point(std::chrono::milliseconds(savedOrigin->at("expiresAtMs").get<int64_t>()));
  ActiveMagicEffectsMap::Entry next{uint32_t(-1),effect,end,savedOrigin};const auto key=ActiveMagicEffectsMap::StackKey(next);
  auto prior=actor.GetActiveMagicEffects().GetByStackKey(data.primaryAV,key);
  if(!restoring&&prior&&savedOrigin&&!savedOrigin->at("operationId").get<std::string>().empty()&&prior->get().provenance&&prior->get().provenance->at("operationId")==savedOrigin->at("operationId"))return true;
  if(restoring){if(!prior)return true;end=prior->get().endTime;}
  const auto old=actor.GetActorValues();
  // Keep the previous timer: a transaction can still abort and restore its
  // row. Instance/key/expiry checks make old callbacks inert after a commit;
  // cancelling it here would leave a restored row without its expiry callback.
  uint32_t timer;const auto id=actor.GetFormId();std::weak_ptr<MpForm> identity=world->LookupFormByIdNoLoad(id);
  const auto instance=savedOrigin?savedOrigin->at("instanceId").get<std::string>():std::string();
  world->SetEffectTimer(std::chrono::duration_cast<std::chrono::milliseconds>(end-std::chrono::system_clock::now()),&timer).Then([world,id,identity,end,key,instance,effectId=effect.effectId,av=data.primaryAV](Viet::Void){auto target=identity.lock(),current=world->LookupFormByIdNoLoad(id);if(!target||current.get()!=target.get()||!target->AsActor())return;const auto active=target->AsActor()->GetActiveMagicEffects().GetByStackKey(av,key);if(active&&active->get().endTime==end&&active->get().data.effectId==effectId&&(!active->get().provenance||active->get().provenance->at("instanceId")==instance)){auto actor=target->AsActor();const auto before=actor->GetActorValues();actor->EditChangeForm([&](MpChangeForm& form){form.activeMagicEffects.RemoveByStackKey(av,key);});actor->ReconcileSrpMaximums(before);}});
  next.timerId=timer;next.endTime=end;
  actor.EditChangeForm([&](MpChangeForm& form){form.activeMagicEffects.AddStacked(data.primaryAV,next);});
  actor.ReconcileSrpMaximums(old);
  return true;
}


void SrpActorModifiers::ReconcileOrigins(MpActor& actor)
{
 if(!Managed(actor))return;
 auto current=Read(actor,true).equipmentEffects;
 const auto fields=actor.GetDynamicFields().GetAsJson();
 if(current.empty()&&!fields.contains("private.srpEquipmentOriginsV1"))return;
 if(!current.empty()&&ActorIdentity(actor).at("incarnation")==""){EnsureIdentity(actor);current=Read(actor,true).equipmentEffects;}
 nlohmann::json ledger={{"schemaVersion",1},{"origins",nlohmann::json::array()}};
 if(fields.contains("private.srpEquipmentOriginsV1")){
  ledger=fields.at("private.srpEquipmentOriginsV1");
  if(ledger.value("schemaVersion",0)!=1||!ledger.contains("origins")||!ledger.at("origins").is_array())throw std::runtime_error("SRP_EQUIPMENT_ORIGIN_CORRUPT");
 }
 std::map<std::string,nlohmann::json> entries;
 for(auto row:ledger.at("origins")){const auto key=row.at("key").get<std::string>();if(!entries.emplace(key,row).second)throw std::runtime_error("SRP_EQUIPMENT_ORIGIN_DUPLICATE");}
 std::set<std::string> live;
 for(const auto& row:current){const auto key=row.at("key").get<std::string>();live.insert(key);entries[key]=row;}
 for(auto& [key,row]:entries)if(!live.count(key)&&row.value("state",std::string())!="REMOVED"){row["state"]="REMOVED";row["removalReason"]="NO_LONGER_IN_VERIFIED_EQUIPMENT";}
 ledger["origins"]=nlohmann::json::array();for(const auto& [key,row]:entries)ledger["origins"].push_back(row);
 if(!fields.contains("private.srpEquipmentOriginsV1")||fields.at("private.srpEquipmentOriginsV1")!=ledger)actor.SetPropertyValueDump("private.srpEquipmentOriginsV1",ledger.dump(),false,false);
}


nlohmann::json SrpActorModifiers::SpellReceipt(MpActor& actor,const std::string& opId)
{
 const auto& fields=actor.GetDynamicFields().GetAsJson();
 if(!fields.contains("private.srpSpellReceiptsV1"))return nullptr;
 const auto& ledger=fields.at("private.srpSpellReceiptsV1");
 if(ledger.value("schemaVersion",0)!=1||!ledger.contains("receipts")||!ledger.at("receipts").is_object())throw std::runtime_error("SRP_SPELL_RECEIPTS_CORRUPT");
 if(!ledger.at("receipts").contains(opId))return nullptr;
 auto receipt=ledger.at("receipts").at(opId);
 if(receipt.at("caster")!=ActorIdentity(actor))throw std::runtime_error("SRP_SPELL_RECEIPT_FOREIGN_CHARACTER");
 // A completed outbox receipt belongs to this caster but its effects live on
 // the other ChangeForm. Do not infer removal by searching the caster's rows.
 if(receipt.value("applicationType",std::string())=="REMOTE_OUTBOX_V1")return receipt;
 if(receipt.at("target")!=ActorIdentity(actor))throw std::runtime_error("SRP_SPELL_RECEIPT_FOREIGN_CHARACTER");
 bool changed=false;
 for(auto& effect:receipt.at("effects"))if(effect.at("state")=="ACTIVE"){
  const auto& origin=effect.at("origin");
  const auto row=actor.GetActiveMagicEffects().GetByStackKey(static_cast<espm::ActorValue>(origin.at("actorValue").get<int>()),origin.at("stackKey").get<std::string>());
  if(!row||!row->get().provenance||row->get().provenance->at("instanceId")!=origin.at("instanceId")){
   effect["state"]="REMOVED";effect["removalReason"]="ACTIVE_INSTANCE_ABSENT";
   effect["removalObservedAtMs"]=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();changed=true;
  }
 }
 if(changed){auto next=ledger;next["receipts"][opId]=receipt;actor.SetPropertyValueDump("private.srpSpellReceiptsV1",next.dump(),false,false);}
 return receipt;
}

nlohmann::json SrpActorModifiers::CastSelf(MpActor& caster,uint32_t spell,const std::string& opId)
{
 if(opId.size()<16||opId.size()>100||!std::all_of(opId.begin(),opId.end(),[](unsigned char c){return std::isalnum(c)||c=='-'||c=='_';}))throw std::runtime_error("SRP_SPELL_OPERATION_ID_INVALID");
 auto world=caster.GetParent();if(!world||!world->HasEspm()||!Managed(caster))throw std::runtime_error("SRP_SPELL_ACTOR_AUTHORITY_REQUIRED");
 const auto source=FormDesc::FromFormId(spell,world->espmFiles).ToString();
 if(auto old=SpellReceipt(caster,opId);!old.is_null()){
  if(old.at("source")!=source)throw std::runtime_error("SRP_SPELL_OPERATION_EQUIVOCATION");
  old["duplicate"]=true;return old;
 }
 if(caster.IsDead())throw std::runtime_error("SRP_SPELL_CASTER_DEAD");
 if(!caster.GetEquipment().IsSpellEquipped(spell))throw std::runtime_error("SRP_SPELL_NOT_EQUIPPED");
 const auto owner=world->GetEspm().GetBrowser().LookupById(spell);
 if(!owner.rec||owner.rec->GetType()!=espm::SPEL::kType)throw std::runtime_error("SRP_SPELL_SOURCE_INVALID");
 const auto learned=caster.GetSpellList();bool knows=std::find(learned.begin(),learned.end(),spell)!=learned.end();
 for(const auto sourceId:{caster.GetBaseId(),caster.GetRaceId()}){
  const auto sourceRecord=world->GetEspm().GetBrowser().LookupById(sourceId);if(!sourceRecord.rec)throw std::runtime_error("SRP_SPELL_KNOWLEDGE_CONTEXT_UNKNOWN");
  std::set<uint32_t> spells;
  if(sourceRecord.rec->GetType()==espm::NPC_::kType)spells=espm::GetData<espm::NPC_>(sourceId,world).spells;
  else if(sourceRecord.rec->GetType()==espm::RACE::kType)spells=espm::GetData<espm::RACE>(sourceId,world).spells;
  for(const auto id:spells)if(sourceRecord.ToGlobalId(id)==spell)knows=true;
 }
 if(!knows)throw std::runtime_error("SRP_SPELL_NOT_LEARNED");
 const auto record=espm::GetData<espm::SPEL>(spell,world);
 if(!record.spellItem||record.spellItem->type!=espm::SPEL::SpellType::Spell||record.spellItem->castType!=espm::SPEL::CastType::FireAndForget||record.spellItem->delivery!=espm::SPEL::Delivery::Self)throw std::runtime_error("SRP_SPELL_DELIVERY_REQUIRES_APPLICATION_PROOF");
 const auto fields=caster.GetDynamicFields().GetAsJson();
 if(fields.value("private.srpSpellCostPolicyV1",std::string())!="RECORD_BASE_ONLY_V1")throw std::runtime_error("SRP_SPELL_COST_AUTHORITY_UNAVAILABLE");
 const auto cost=record.spellItem->spellCost;
 if(!cost||cost>100000||!std::isfinite(record.spellItem->chargeTime)||record.spellItem->chargeTime<0||record.spellItem->chargeTime>60)throw std::runtime_error("SRP_SPELL_COST_OR_CHARGE_UNSUPPORTED");
 if(!Read(caster).complete)throw std::runtime_error("SRP_SPELL_MODIFIER_CONTEXT_UNKNOWN");
 const auto effects=SourceEffects(spell,world);if(effects.empty()||effects.size()>128)throw std::runtime_error("SRP_SPELL_EFFECT_SET_UNSUPPORTED");
 for(uint32_t i=0;i<effects.size();++i){
  const auto data=EffectData(effects[i].effectId,world);
  if(data.IsFlagSet(espm::MGEF::Flags::NoRecast)||data.IsFlagSet(espm::MGEF::Flags::DispelEffects))throw std::runtime_error("SRP_SPELL_STACKING_POLICY_NOT_PROVEN");
  const bool restore=data.effectType==espm::MGEF::EffectType::ValueMod&&!data.IsFlagSet(espm::MGEF::Flags::Recover)&&!data.IsFlagSet(espm::MGEF::Flags::Detrimental)&&effects[i].duration==0&&(data.primaryAV==espm::ActorValue::Health||data.primaryAV==espm::ActorValue::Magicka||data.primaryAV==espm::ActorValue::Stamina);
  if(effects[i].areaOfEffect||(!Peak(data)&&!restore)||data.IsFlagSet(espm::MGEF::Flags::NoMagnitude))throw std::runtime_error("SRP_SPELL_EFFECT_APPLICATION_UNSUPPORTED");
  Magnitude(effects[i],data);
  if(TestConditions(caster,owner,i,&caster)==Result::Unknown||EffectConditions(caster,effects[i].effectId,&caster)==Result::Unknown)throw std::runtime_error("SRP_SPELL_CONDITIONS_UNKNOWN");
 }
 const auto now=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
 nlohmann::json ledger={{"schemaVersion",1},{"receipts",nlohmann::json::object()}};
 if(fields.contains("private.srpSpellReceiptsV1"))ledger=fields.at("private.srpSpellReceiptsV1");
 if(ledger.value("schemaVersion",0)!=1||!ledger.at("receipts").is_object())throw std::runtime_error("SRP_SPELL_RECEIPTS_CORRUPT");
 if(ledger.at("receipts").size()>=4096)throw std::runtime_error("SRP_SPELL_RECEIPT_CAPACITY_REQUIRES_RECONCILIATION");
 for(const auto& [key,receipt]:ledger.at("receipts").items())if(receipt.at("caster").at("characterId")==ActorIdentity(caster).at("characterId")&&now-receipt.at("appliedAtMs").get<int64_t>()<std::max<int64_t>(500,int64_t(record.spellItem->chargeTime*1000)))throw std::runtime_error("SRP_SPELL_CAST_RATE_LIMIT");
 const auto values=caster.GetActorValues();if(values.magicka*values.magickaPercentage<float(cost))throw std::runtime_error("SRP_SPELL_INSUFFICIENT_MAGICKA");
 nlohmann::json result;
 caster.RunChangeFormTransaction([&](){
  EnsureIdentity(caster);
  result={{"schemaVersion",1},{"sourceType","SPEL"},{"source",source},{"operationId",opId},{"caster",ActorIdentity(caster)},{"target",ActorIdentity(caster)},{"appliedAtMs",now},{"cost",cost},{"costPolicy","RECORD_BASE_ONLY_V1"},{"state","APPLIED"},{"effects",nlohmann::json::array()}};
  caster.DamageActorValue(espm::ActorValue::Magicka,cost);
  for(uint32_t i=0;i<effects.size();++i){
   auto effect=effects[i];auto origin=CreateProvenance(caster,spell,i,caster,opId);
   const auto sourceCondition=TestConditions(caster,owner,i,&caster),effectCondition=EffectConditions(caster,effect.effectId,&caster);
   const bool active=sourceCondition==Result::Active&&effectCondition==Result::Active;
   origin["sourceConditionAtApply"]=sourceCondition==Result::Active?"TRUE":"FALSE";
   origin["sourceConditionCount"]=Conditions(owner.rec,world,i).values.size();
   if(active)caster.ApplyMagicEffect(effect,false,false,&origin);
   result["effects"].push_back({{"origin",origin},{"magnitude",effect.magnitude},{"duration",effect.duration},{"expiresAtMs",origin.at("expiresAtMs")},{"state",!active?"CONDITION_FALSE":Peak(EffectData(effect.effectId,world))?"ACTIVE":"INSTANT_COMPLETE"}});
  }
  ledger["receipts"][opId]=result;caster.SetPropertyValueDump("private.srpSpellReceiptsV1",ledger.dump(),false,false);return true;
 });
 result["duplicate"]=false;return result;
}

nlohmann::json SrpActorModifiers::SpellAuthority(const MpActor& actor,uint32_t spell,bool dual)
{
 auto world=actor.GetParent();if(!world||!world->HasEspm())throw std::runtime_error("SRP_SPELL_RECORD_AUTHORITY_UNAVAILABLE");
 const auto owner=world->GetEspm().GetBrowser().LookupById(spell);
 if(!owner.rec||owner.rec->GetType()!=espm::SPEL::kType)throw std::runtime_error("SRP_SPELL_SOURCE_INVALID");
 const auto data=espm::GetData<espm::SPEL>(spell,world);if(!data.spellItem)throw std::runtime_error("SRP_SPELL_SPIT_MISSING");
 nlohmann::json effects=nlohmann::json::array();
 for(const auto& effect:SourceEffects(spell,world)){
  const auto record=world->GetEspm().GetBrowser().LookupById(effect.effectId);if(!record.rec)throw std::runtime_error("SRP_SPELL_MGEF_MISSING");
  nlohmann::json metadata={{"mgef",FormDesc::FromFormId(effect.effectId,world->espmFiles).ToString()},{"school",nullptr},{"baseCost",nullptr}};
  espm::RecordHeaderAccess::IterateFields(record.rec,[&](const char* type,uint32_t size,const char* bytes){if(memcmp(type,"DATA",4)||size<20)return;float cost;int32_t school;memcpy(&cost,bytes+4,4);memcpy(&school,bytes+12,4);if(std::isfinite(cost)&&cost>=0)metadata["baseCost"]=cost;metadata["school"]=school;},world->GetEspmCache());
  effects.push_back(metadata);
 }
 // The installed Platform only observes TESSpellCastEvent; it cannot suppress
 // precisely one cast/cost/effect before application. Client cost/LOS reports
 // cannot supply server authority. Do not fall back to the R37.2 base tariff.
 return {{"schemaVersion",2},{"source",FormDesc::FromFormId(spell,world->espmFiles).ToString()},{"caster",ActorIdentity(actor)},
 {"recordBaseCost",data.spellItem->spellCost},{"autoCalculate",!(static_cast<uint32_t>(data.spellItem->flags)&1u)},
 {"castType",static_cast<int>(data.spellItem->castType)},{"delivery",static_cast<int>(data.spellItem->delivery)},
 {"perk",data.spellItem->perkFormId? nlohmann::json(FormDesc::FromFormId(owner.ToGlobalId(data.spellItem->perkFormId),world->espmFiles).ToString()):nlohmann::json(nullptr)},
 {"effects",effects},{"dualCast",dual},{"policy","ENGINE_LEGACY_ONLY_NO_ADDITIONAL_SERVER_APPLICATION"},
 {"finalCost",nullptr},{"applicationAllowed",false},{"engineControlSupported",false},{"finalCostAuthoritySupported",false},
 {"blockedBy",nlohmann::json::array({"PRECAST_ENGINE_CONTROL_UNAVAILABLE","FINAL_COST_RULES_AND_RUNTIME_CONTEXT_UNAVAILABLE",dual?"DUAL_CAST_RULES_UNPROVEN":"NO_CLIENT_COST_AUTHORITY"})}};
}

nlohmann::json SrpActorModifiers::CastPreflight(const MpActor& caster,uint32_t spell,const MpActor* target,bool dual)
{
 auto result=SpellAuthority(caster,spell,dual);auto world=caster.GetParent();
 nlohmann::json checks=nlohmann::json::object();
 auto check=[&](const char* key,Result state,const char* producer){checks[key]={{"state",state==Result::Active?"TRUE":state==Result::Inactive?"FALSE":"UNKNOWN"},{"producer",producer}};};
 auto truth=[](bool b){return b?Result::Active:Result::Inactive;};
 auto persistent=[](const MpActor& a){auto id=ActorIdentity(a);return !id.at("characterId").get<std::string>().empty()&&!id.at("incarnation").get<std::string>().empty()&&a.GetProfileId()>0;};
 auto managed=[](const MpActor& a){return Managed(a)&&SrpActorStats::Read(a).enabled&&Read(a).complete;};
 const auto owner=world->GetEspm().GetBrowser().LookupById(spell);const auto record=espm::GetData<espm::SPEL>(spell,world);
 check("casterIdentity",truth(persistent(caster)),"native private identity/profile/character");
 check("casterAlive",truth(!caster.IsDead()),"MpActor::IsDead");
 check("casterResources",truth(managed(caster)),"SrpActorStats::Read + existing modifier composer");
 check("spellEquipped",truth(caster.GetEquipment().IsSpellEquipped(spell)),"native Equipment");
 bool known=false,knowledgeComplete=true;for(auto id:caster.GetSpellList())if(id==spell)known=true;
 for(auto id:{caster.GetBaseId(),caster.GetRaceId()}){
  const auto rec=world->GetEspm().GetBrowser().LookupById(id);if(!rec.rec){knowledgeComplete=false;continue;}std::set<uint32_t> list;
  if(rec.rec->GetType()==espm::NPC_::kType)list=espm::GetData<espm::NPC_>(id,world).spells;
  if(rec.rec->GetType()==espm::RACE::kType)list=espm::GetData<espm::RACE>(id,world).spells;
  for(auto s:list)if(rec.ToGlobalId(s)==spell)known=true;
 }
 check("spellKnown",known?Result::Active:knowledgeComplete?Result::Inactive:Result::Unknown,"native learnedSpells/NPC/RACE; missing records remain UNKNOWN");
 check("precastControl",Result::Unknown,"no installed Platform pre-cast admission hook");
 check("finalCost",Result::Unknown,"no complete server cost/skill/perk/ENCH producer");
 check("uniqueImpact",Result::Unknown,"legacy packets carry no durable impact identity");
 check("instance",Result::Unknown,"no separate authoritative spell instance epoch");
 check("lineOfSight",Result::Unknown,"no server collision/LOS world in current SkyMP");
 check("collision",Result::Unknown,"client hit/projectile is an untrusted observation");
 check("dualCast",Result::Unknown,"requested dual mode is not proof of actual engine mode or cost policy");
 result["target"]=target?ActorIdentity(*target):nlohmann::json(nullptr);
 if(!target||target->GetParent()!=world){
  check("targetIdentity",Result::Unknown,"target actor not resolved in this WorldState");
  check("sameWorld",Result::Unknown,"target actor unavailable");
  check("targetAlive",Result::Unknown,"target actor unavailable");
  check("targetResources",Result::Unknown,"target actor unavailable");
  check("range",Result::Unknown,"target actor unavailable");
  check("effectConditions",Result::Unknown,"target actor unavailable");
  check("targetHost",Result::Unknown,"target actor unavailable");
 }else{
  check("targetIdentity",truth(persistent(*target)),"native private identity/profile/character");
  check("targetAlive",truth(!target->IsDead()),"MpActor::IsDead");
  check("targetResources",truth(managed(*target)),"SrpActorStats::Read + existing modifier composer");
  const bool sameWorld=caster.GetCellOrWorld()==target->GetCellOrWorld();
  check("sameWorld",truth(sameWorld),"native accepted cellOrWorld; not independent collision proof");
  const auto d=(caster.GetPos()-target->GetPos()).SqrLength();
  result["distanceSquared"]=std::isfinite(d)?nlohmann::json(d):nlohmann::json(nullptr);
  result["recordRange"]=record.spellItem->range;
  Result range=Result::Unknown;
  if(!sameWorld)range=Result::Inactive;
  else if(record.spellItem->delivery==espm::SPEL::Delivery::Self)range=truth(&caster==target);
  check("range",range,"Self exact actor only; non-Self SPIT units/defaults and projectile travel authority unproven");
  Result conditions=Result::Active;const auto effects=SourceEffects(spell,world);
  for(uint32_t i=0;i<effects.size();++i){const auto a=TestConditions(*target,owner,i,&caster),b=EffectConditions(*target,effects[i].effectId,&caster);if(a==Result::Unknown||b==Result::Unknown){conditions=Result::Unknown;break;}if(a==Result::Inactive||b==Result::Inactive)conditions=Result::Inactive;}
  check("effectConditions",conditions,"source EFID/MGEF tri-state with explicit native caster/target");
  const auto host=world->hosters.find(target->GetFormId());result["targetHostReference"]=host==world->hosters.end()?nlohmann::json(nullptr):nlohmann::json(FormDesc::FromFormId(host->second,world->espmFiles).ToString());
  // Hoster ownership authorizes transport, never proves an engine collision.
  check("targetHost",Result::Unknown,"host map observation only; actor class/host epoch not a spell-impact grant");
 }
 result["authorityContract"]="SRP-SPELL-AUTHORITY-R374";result["checks"]=checks;
 result["evidenceType"]="NATIVE_READONLY_PREFLIGHT_NOT_IMPACT_PROOF";result["applicationAllowed"]=false;
 result["blockedBy"]=nlohmann::json::array();
 for(const auto& [key,row]:checks.items())if(row.at("state")!="TRUE")result["blockedBy"].push_back(key+":"+row.at("state").get<std::string>());
 return result;
}
