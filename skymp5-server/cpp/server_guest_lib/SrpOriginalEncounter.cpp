#include "SrpOriginalEncounter.h"
#include "SrpOriginalStats.h"
#include "SrpOriginalLifecycle.h"
#include "SrpActorStats.h"
#include "EvaluateTemplate.h"
#include "MpActor.h"
#include "libespm/GroupUtils.h"
#include "libespm/RecordHeaderAccess.h"
#include "libespm/ReadUnaligned.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <sodium.h>
#include <stdexcept>

namespace {
void Require(bool ok, const char* error)
{
  if(!ok) throw std::runtime_error(error);
}
std::string Digest(nlohmann::json value)
{
  value.erase("checksum");const auto text=value.dump();
  unsigned char bytes[crypto_hash_sha256_BYTES];
  crypto_hash_sha256(bytes,reinterpret_cast<const unsigned char*>(text.data()),text.size());
  constexpr char hex[]="0123456789abcdef";std::string result;
  for(auto b:bytes){result+=hex[b>>4];result+=hex[b&15];}
  return result;
}
std::string Desc(const WorldState& world,uint32_t id)
{
  return FormDesc::FromFormId(id,world.espmFiles).ToString();
}
uint32_t ZoneId(WorldState& world,const espm::LookupResult& record)
{
  uint32_t id=0;bool found=false;
  if(!record.rec)return 0;
  espm::RecordHeaderAccess::IterateFields(record.rec,
    [&](const char* type,uint32_t size,const char* bytes){
      if(!std::memcmp(type,"XEZN",4)){
        Require(!found && size==4,"SRP_ENCOUNTER_XEZN_CORRUPT");found=true;
        const auto local=espm::ReadFieldScalar<uint32_t>(bytes,size);
        id=local?record.ToGlobalId(local):0;
      }
    },world.GetEspmCache());
  return id;
}
struct Frame {
  nlohmann::json placement,stats,zone=nullptr;
  espm::NPC_::Data npc;
  std::string scope;
};
Frame Describe(const MpActor& actor)
{
  auto& world=*actor.GetParent();auto& browser=world.GetEspm().GetBrowser();
  const auto ref=browser.LookupById(actor.GetFormId());
  Require(ref.rec && ref.rec->GetType()=="ACHR","SRP_ENCOUNTER_ACHR_REQUIRED");
  uint32_t localCell=0;
  const auto* group=espm::GetCellGroup(browser,ref.rec);
  Require(group && group->GetParentCELL(localCell),"SRP_ENCOUNTER_CELL_REQUIRED");
  const auto cellId=ref.ToGlobalId(localCell);
  const auto cell=browser.LookupById(cellId);
  Require(cell.rec && cell.rec->GetType()=="CELL","SRP_ENCOUNTER_CELL_REQUIRED");
  const auto location=ref.ToGlobalId(espm::GetWorldOrCell(browser,ref.rec));
  Require(location && actor.GetCellOrWorld().ToFormId(world.espmFiles)==location,
    "SRP_ENCOUNTER_LOCATION_MISMATCH");
  const auto wrld=browser.LookupById(location);
  auto zoneId=ZoneId(world,ref);std::string source="ACHR";
  if(!zoneId){zoneId=ZoneId(world,cell);source="CELL";}
  if(!zoneId && wrld.rec && wrld.rec->GetType()=="WRLD"){
    zoneId=ZoneId(world,wrld);source="WRLD";
  }
  Frame out;
  if(zoneId){
    const auto zone=browser.LookupById(zoneId);
    Require(zone.rec && zone.rec->GetType()=="ECZN" && !(zone.rec->GetFlags()&(0x20|0x1000)),
      "SRP_ENCOUNTER_ECZN_REQUIRED");
    bool found=false;
    espm::RecordHeaderAccess::IterateFields(zone.rec,
      [&](const char* type,uint32_t size,const char* bytes){
        if(!std::memcmp(type,"DATA",4)){
          Require(!found && size==12,"SRP_ENCOUNTER_ECZN_DATA_CORRUPT");found=true;
          out.zone={{"reference",Desc(world,zoneId)},
            {"minimum",espm::ReadFieldScalar<uint8_t>(bytes,size,9)},
            {"flags",espm::ReadFieldScalar<uint8_t>(bytes,size,10)},
            {"maximum",espm::ReadFieldScalar<uint8_t>(bytes,size,11)}};
        }
      },world.GetEspmCache());
    Require(found,"SRP_ENCOUNTER_ECZN_DATA_REQUIRED");
    out.scope="ECZN:"+Desc(world,zoneId);
  } else {source="NONE";out.scope="ACHR:"+Desc(world,actor.GetFormId());}
  out.placement={{"reference",Desc(world,actor.GetFormId())},{"cell",Desc(world,cellId)},
    {"worldOrCell",Desc(world,location)},{"zoneSource",source},{"scope",out.scope}};
  const auto stats=EvaluateTemplate<espm::NPC_::UseStats>(&world,actor.GetBaseId(),
    actor.GetChangeForm().templateChain,[](const auto& record,const auto& npc){return std::make_pair(record,npc);});
  out.npc=stats.second;
  Require(out.npc.hasStatsConfiguration,"SRP_ENCOUNTER_ACBS_REQUIRED");
  nlohmann::json chain=nlohmann::json::array();
  for(const auto& desc:actor.GetChangeForm().templateChain)chain.push_back(desc.ToString());
  out.stats={{"source",Desc(world,stats.first.ToGlobalId(stats.first.rec->GetId()))},
    {"pcLevelMult",out.npc.pcLevelMult},{"value",out.npc.level},
    {"minimum",out.npc.calcMinLevel},{"maximum",out.npc.calcMaxLevel},{"templates",chain}};
  return out;
}
nlohmann::json Scaling(WorldState& world)
{
  nlohmann::json result=nullptr;
  for(const auto& r:world.GetEspm().GetBrowser().GetDistinctRecordsByType("GMST")){
    if(std::strcmp(r.rec->GetEditorId(world.GetEspmCache()),"fLevelScalingMult"))continue;
    Require(result.is_null(),"SRP_ENCOUNTER_GMST_AMBIGUOUS");bool found=false;
    espm::RecordHeaderAccess::IterateFields(r.rec,
      [&](const char* type,uint32_t size,const char* bytes){
        if(!std::memcmp(type,"DATA",4)){
          Require(!found && size==4,"SRP_ENCOUNTER_GMST_CORRUPT");found=true;
          const float v=espm::ReadFieldScalar<float>(bytes,size);
          Require(std::isfinite(v) && v>=0 && v<=10,"SRP_ENCOUNTER_GMST_RANGE");
          result={{"value",v},{"source",Desc(world,r.ToGlobalId(r.rec->GetId()))}};
        }
      },world.GetEspmCache());
    Require(found,"SRP_ENCOUNTER_GMST_REQUIRED");
  }
  Require(!result.is_null(),"SRP_ENCOUNTER_GMST_REQUIRED");return result;
}
void Validate(const nlohmann::json& receipt,const nlohmann::json& identity)
{
  Require(identity.is_object() && identity.value("schemaVersion",0)==1 &&
    identity.value("lifecycleEpoch",-1)==0 && identity.value("rewardsEnabled",true)==false &&
    identity.at("instanceId").is_string() && !identity.at("instanceId").get<std::string>().empty(),
    "SRP_ENCOUNTER_IDENTITY_CORRUPT");
  Require(receipt.is_object() && receipt.value("schemaVersion",0)==1 &&
    receipt.value("checksum",std::string())==Digest(receipt) && receipt.at("identity")==identity &&
    receipt.at("operationId")==identity.at("instanceId").get<std::string>()+":encounter:0" &&
    receipt.at("finalLevel").is_number_integer() && receipt.at("finalLevel")>=1 && receipt.at("finalLevel")<=1000,
    "SRP_ENCOUNTER_RECEIPT_CORRUPT");
  const auto& zone=receipt.at("zoneContext");
  Require(zone.is_object() && zone.value("schemaVersion",0)==1 &&
    zone.value("rulesVersion",std::string())=="SRP-FIRST-CONFIRMED-ENCOUNTER-1" &&
    zone.value("checksum",std::string())==Digest(zone) &&
    zone.at("scope")==receipt.at("placement").at("scope") &&
    zone.at("zoneLevel").is_number_integer() && zone.at("zoneLevel")>=1 && zone.at("zoneLevel")<=1000,
    "SRP_ENCOUNTER_ZONE_CONTEXT_CORRUPT");
  const auto& pc=zone.at("pc");
  Require(pc.at("level").is_number_integer() && pc.at("level")>=1 && pc.at("level")<=200 &&
    pc.at("revision").is_number_integer() && pc.at("revision")>0 &&
    pc.at("profileId").is_string() && !pc.at("profileId").get<std::string>().empty() &&
    pc.at("characterId").is_string() && !pc.at("characterId").get<std::string>().empty(),
    "SRP_ENCOUNTER_PC_PROVENANCE_CORRUPT");
  const auto& definition=zone.at("zone");
  const auto expectedZone=definition.is_null()?pc.at("level").get<uint16_t>():
    SrpOriginalEncounter::ZoneLevel(pc.at("level"),definition.at("minimum"),
      definition.at("maximum"),definition.at("flags"),zone.at("scaling").at("value"));
  Require(zone.at("zoneLevel")==expectedZone,"SRP_ENCOUNTER_ZONE_LEVEL_MISMATCH");
  espm::NPC_::Data data;data.hasStatsConfiguration=true;
  data.pcLevelMult=receipt.at("stats").at("pcLevelMult");
  data.level=receipt.at("stats").at("value");data.calcMinLevel=receipt.at("stats").at("minimum");
  data.calcMaxLevel=receipt.at("stats").at("maximum");
  Require(receipt.at("finalLevel")==SrpOriginalStats::ResolveLevel(data,expectedZone),
    "SRP_ENCOUNTER_FINAL_LEVEL_MISMATCH");
}
bool Durable(const MpActor& actor,const nlohmann::json& receipt)
{
  bool saved=false;
  for(const auto& image:actor.GetParent()->GetSrpOriginalEncounterImages()){
    const auto& other=image.at("receipt");Validate(other,image.at("identity"));
    if(other.at("zoneContext").at("scope")==receipt.at("zoneContext").at("scope"))
      Require(other.at("zoneContext")==receipt.at("zoneContext"),"SRP_ENCOUNTER_ZONE_CONFLICT");
    if(image.at("reference")==receipt.at("identity").at("reference") &&
       other==receipt && image.at("durable")==true)saved=true;
  }
  return saved;
}
}

uint16_t SrpOriginalEncounter::ZoneLevel(uint16_t pc,uint8_t minimum,uint8_t maximum,
  uint8_t flags,float scaling)
{
  Require(pc>=1 && pc<=200 && (!maximum || minimum<=maximum) && !(flags&~7) &&
    std::isfinite(scaling) && scaling>=0 && scaling<=10,"SRP_ENCOUNTER_ZONE_INPUT_INVALID");
  // AE 1.6.1170 RVA 28a210: min + trunc((PC-min)*fLevelScalingMult)
  // when PC >= min; below min, MatchPCBelowMinimum bypasses the lower clamp.
  float scaled=pc;
  if(pc<minimum){if(!(flags&2))scaled=minimum;}
  else {const float growth=float(pc-minimum)*scaling;scaled=float(minimum)+growth;}
  Require(scaled<65536,"SRP_ENCOUNTER_ZONE_LEVEL_OVERFLOW");
  uint32_t level=uint32_t(scaled);
  if(maximum && level>maximum)level=maximum;
  // TESObjectREFR::GetEncounterLevel(false), RVA 2fa605, clamps the seed to 1.
  level=std::max(level,1u);
  Require(level<=1000,"SRP_ENCOUNTER_ZONE_LEVEL_OVERFLOW");return uint16_t(level);
}

std::optional<nlohmann::json> SrpOriginalEncounter::Read(const MpActor& actor,bool requireDurable)
{
  const auto& fields=actor.GetDynamicFields().GetAsJson();
  if(!fields.contains(kProperty))return std::nullopt;
  const auto receipt=fields.at(kProperty),identity=SrpOriginalLifecycle::Inspect(actor).at("identity");
  Validate(receipt,identity);
  if(requireDurable)Require(Durable(actor,receipt),"SRP_ENCOUNTER_SAVE_PENDING");
  return std::optional<nlohmann::json>{std::in_place,receipt};
}

nlohmann::json SrpOriginalEncounter::Capture(MpActor& actor,const MpActor& entrant)
{
  Require(actor.GetParent() && actor.GetParent()->HasEspm(),"SRP_ENCOUNTER_ESPM_REQUIRED");
  auto& world=*actor.GetParent();const auto life=SrpOriginalLifecycle::Inspect(actor);
  const auto frame=Describe(actor);
  const auto current=Read(actor);
  if(current){
    Require(current->at("placement")==frame.placement && current->at("stats")==frame.stats,
      "SRP_ENCOUNTER_MIGRATION_REQUIRED");
    const auto g=world.GetChangeFormGeneration(actor.GetFormId());
    return {{"receipt",*current},{"duplicate",true},{"generation",g},{"saved",Durable(actor,*current)}};
  }
  Require(life.at("state")=="ALIVE_OBSERVED" && life.at("saved")==true,
    "SRP_ENCOUNTER_SAVED_IDENTITY_REQUIRED");
  Require(entrant.GetParent()==&world && entrant.GetFormId()>=0xff000000 &&
    entrant.GetProfileId()>0 && !entrant.IsDead() && !entrant.IsDisabled() &&
    entrant.GetCellOrWorld()==actor.GetCellOrWorld(),"SRP_ENCOUNTER_ENTRANT_INVALID");
  const auto delta=entrant.GetPos()-actor.GetPos();
  Require(std::isfinite(delta.SqrLength()) && delta.SqrLength()<=4096.f*4096.f,
    "SRP_ENCOUNTER_ENTRANT_TOO_FAR");
  const auto fields=entrant.GetDynamicFields().GetAsJson();
  Require(fields.contains("srpProgression") && fields.contains("srpActiveCharacterId"),
    "SRP_ENCOUNTER_CANONICAL_PROGRESSION_REQUIRED");
  const auto pc=SrpActorStats::FromProjection(fields.at("srpProgression"));
  Require(pc.profileId==std::to_string(entrant.GetProfileId()) &&
    fields.at("srpActiveCharacterId")==pc.characterId && SrpActorStats::Read(entrant).revision<=pc.revision,
    "SRP_ENCOUNTER_CANONICAL_OWNER_MISMATCH");
  nlohmann::json shared=nullptr;bool sharedSaved=false;
  for(const auto& image:world.GetSrpOriginalEncounterImages()){
    const auto& r=image.at("receipt");Validate(r,image.at("identity"));
    if(r.at("zoneContext").at("scope")!=frame.scope)continue;
    const auto& proposed=r.at("zoneContext");
    Require(proposed.at("zone")==frame.zone,"SRP_ENCOUNTER_ZONE_DEFINITION_CHANGED");
    Require(shared.is_null() || shared==proposed,"SRP_ENCOUNTER_ZONE_CONFLICT");
    shared=proposed;sharedSaved=sharedSaved||image.at("durable").get<bool>();
  }
  if(!shared.is_null())Require(sharedSaved,"SRP_ENCOUNTER_SAVE_PENDING");
  else {
    const auto scaling=frame.zone.is_null()?nlohmann::json():Scaling(world);
    const auto zoneLevel=frame.zone.is_null()?uint16_t(pc.level):ZoneLevel(uint16_t(pc.level),
      frame.zone.at("minimum"),frame.zone.at("maximum"),frame.zone.at("flags"),scaling.at("value"));
    shared={{"schemaVersion",1},{"rulesVersion","SRP-FIRST-CONFIRMED-ENCOUNTER-1"},
      {"scope",frame.scope},{"zone",frame.zone},{"scaling",scaling},{"zoneLevel",zoneLevel},
      {"anchor",{{"reference",life.at("identity").at("reference")},{"instanceId",life.at("identity").at("instanceId")}}},
      {"pc",{{"actor",Desc(world,entrant.GetFormId())},{"profileId",pc.profileId},
        {"characterId",pc.characterId},{"revision",pc.revision},{"level",pc.level},
        {"projectionChecksum",Digest(fields.at("srpProgression"))}}}};
    shared["checksum"]=Digest(shared);
  }
  // Multiplayer policy: the persisted zone seed replaces the ephemeral global
  // PC input. This is explicit server policy, not a claim of engine parity.
  const auto finalLevel=SrpOriginalStats::ResolveLevel(frame.npc,shared.at("zoneLevel").get<uint16_t>());
  nlohmann::json receipt={{"schemaVersion",1},{"identity",life.at("identity")},
    {"operationId",life.at("identity").at("instanceId").get<std::string>()+":encounter:0"},
    {"placement",frame.placement},{"stats",frame.stats},{"zoneContext",shared},{"finalLevel",finalLevel}};
  receipt["checksum"]=Digest(receipt);
  const auto beforeGeneration=world.GetChangeFormGeneration(actor.GetFormId());
  actor.RunChangeFormTransaction([&]{actor.SetPropertyValueDump(kProperty,receipt.dump(),false,false);return true;});
  const auto generation=world.GetChangeFormGeneration(actor.GetFormId());
  Require(generation>beforeGeneration,"SRP_ENCOUNTER_SAVE_NOT_REQUESTED");
  // Observe this exact generation before unrelated actor writes can advance it.
  const bool saved=Durable(actor,receipt);
  return {{"receipt",receipt},{"duplicate",false},{"generation",generation},{"saved",saved}};
}
