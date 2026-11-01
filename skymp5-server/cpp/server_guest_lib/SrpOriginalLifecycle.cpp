#include "SrpOriginalLifecycle.h"
#include "MpActor.h"
#include "WorldState.h"
#include <chrono>
#include <cmath>
#include <stdexcept>

namespace {
constexpr auto originKey = "private.srpOriginalReferenceV1";
constexpr auto lifeKey = "private.srpOriginalLifecycleV1";
void Require(bool accepted, const char* message)
{
  if (!accepted) throw std::runtime_error(message);
}
nlohmann::json Identity(const MpActor& actor)
{
  const auto fields=actor.GetDynamicFields().GetAsJson();
  Require(fields.contains(originKey), "SRP_ORIGINAL_IDENTITY_REQUIRED");
  const auto& p=fields.at(originKey);auto* world=actor.GetParent();
  Require(world && actor.GetFormId()<0xff000000 && actor.GetProfileId()<0 &&
    !actor.IsCreatedAsPlayer() && (!fields.contains("srpActiveCharacterId") ||
      fields.at("srpActiveCharacterId").is_null() || fields.at("srpActiveCharacterId")==""),
    "SRP_ORIGINAL_PLAYER_FORBIDDEN");
  Require(p.is_object() && p.value("schemaVersion",0)==1 && p.value("lifecycleEpoch",-1)==0 &&
    p.value("rewardsEnabled",true)==false && p.contains("instanceId") &&
    p.at("instanceId").is_string() && !p.at("instanceId").get<std::string>().empty() &&
    p.value("reference",std::string())==FormDesc::FromFormId(actor.GetFormId(),world->espmFiles).ToString() &&
    p.value("base",std::string())==FormDesc::FromFormId(actor.GetBaseId(),world->espmFiles).ToString(),
    "SRP_ORIGINAL_IDENTITY_CORRUPT");
  return p;
}
void ValidateDeath(const nlohmann::json& state,const nlohmann::json& identity)
{
  Require(state.is_object() &&
    state.value("reference",std::string())==identity.at("reference").get<std::string>() &&
    state.value("instanceId",std::string())==identity.at("instanceId").get<std::string>() &&
    state.contains("death") && state.at("death").is_object(),
    "SRP_ORIGINAL_LIFECYCLE_CORRUPT");
  const auto& death=state.at("death");
  const int schema=state.value("schemaVersion",0);
  if(schema==1){
    // Legacy quarantine receipt: still readable, still refuses rewards.
    Require(state.value("epoch",-1)==0 && state.value("state",std::string())=="DEAD_QUARANTINED" &&
      state.value("lootEnabled",true)==false && state.value("xpEnabled",true)==false &&
      state.value("respawnEnabled",true)==false,
      "SRP_ORIGINAL_LIFECYCLE_CORRUPT");
    Require(death.value("operationId",std::string())==identity.at("instanceId").get<std::string>()+":death:0" &&
      death.value("source",std::string())=="NATIVE_KILL_OBSERVATION" &&
      death.value("rewardAuthority",true)==false && death.value("nativeDropsMaterialized",false)==true,
      "SRP_ORIGINAL_DEATH_RECEIPT_CORRUPT");
    return;
  }
  Require(schema==2,"SRP_ORIGINAL_LIFECYCLE_CORRUPT");
  // Authoritative receipt: the operation that killed it, the killer identity and
  // the epoch the life belonged to are all pinned; a later epoch cannot reuse
  // the same operation id.
  Require(state.value("epoch",-1)>=0 && state.value("state",std::string())=="DEAD" &&
    state.value("respawnEnabled",true)==true &&
    death.value("source",std::string())=="SRP_ORIGINAL_IMPACT" &&
    death.value("rewardAuthority",true)==true &&
    !death.value("operationId",std::string()).empty() &&
    state.at("death").contains("killer"),
    "SRP_ORIGINAL_DEATH_RECEIPT_CORRUPT");
}
}

bool SrpOriginalLifecycle::Managed(const MpActor& actor)
{
  const auto& fields=actor.GetDynamicFields().GetAsJson();
  // Corruption must not downgrade a corpse into the unguarded legacy path.
  // An orphan lifecycle marker still quarantines interaction and respawn;
  // Inspect/Kill will reject the missing identity without erasing the receipt.
  return fields.contains(originKey)||fields.contains(lifeKey)||
    (actor.GetParent()&&actor.GetParent()->IsSrpOriginalControlled(actor.GetFormId()));
}

nlohmann::json SrpOriginalLifecycle::Inspect(const MpActor& actor)
{
  const auto identity=Identity(actor),fields=actor.GetDynamicFields().GetAsJson();
  const auto state=fields.contains(lifeKey)?fields.at(lifeKey):nlohmann::json();
  if(!state.is_null()){
    ValidateDeath(state,identity);
    Require(actor.IsDead(),"SRP_ORIGINAL_DEATH_STATE_MISMATCH");
  }
  auto* world=actor.GetParent();
  const auto generation=world->GetChangeFormGeneration(actor.GetFormId());
  const auto host=world->hosters.find(actor.GetFormId());
  const bool authoritative=!state.is_null()&&state.value("schemaVersion",0)==2;
  return {{"identity",identity},{"lifecycle",state},
    {"state",state.is_null()?(actor.IsDead()?"LEGACY_DEATH_REQUIRES_REVIEW":"ALIVE_OBSERVED"):
      (authoritative?"DEAD":"DEAD_QUARANTINED")},
    {"generation",generation},{"saved",generation>0&&world->IsChangeFormSaved(actor.GetFormId(),generation)},
    {"hostActorId",host==world->hosters.end()?0:host->second},
    {"hostEvidence","TRANSPORT_OWNER_NOT_COLLISION_OR_REWARD_AUTHORITY"},
    {"lootEnabled",authoritative},{"xpEnabled",authoritative},{"respawnEnabled",authoritative}};
}

void SrpOriginalLifecycle::RecordDeath(MpActor& actor,const MpActor* killer)
{
  const auto identity=Identity(actor),fields=actor.GetDynamicFields().GetAsJson();
  Require(actor.IsDead(),"SRP_ORIGINAL_DEATH_NOT_OBSERVED");
  Require(!fields.contains(lifeKey),"SRP_ORIGINAL_DEATH_ALREADY_RECORDED");
  nlohmann::json origin=nullptr;
  if(killer){
    const auto k=killer->GetDynamicFields().GetAsJson();
    origin={{"reference",FormDesc::FromFormId(killer->GetFormId(),actor.GetParent()->espmFiles).ToString()},
      {"profileId",std::to_string(killer->GetProfileId())},
      {"characterId",k.value("srpActiveCharacterId",std::string())}};
  }
  const auto now=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
  const nlohmann::json state={{"schemaVersion",1},{"reference",identity.at("reference")},
    {"instanceId",identity.at("instanceId")},{"epoch",0},{"state","DEAD_QUARANTINED"},
    {"lootEnabled",false},{"xpEnabled",false},{"respawnEnabled",false},
    {"death",{{"operationId",identity.at("instanceId").get<std::string>()+":death:0"},
      {"source","NATIVE_KILL_OBSERVATION"},{"rewardAuthority",false},
      {"nativeDropsMaterialized",true},{"killer",origin},{"observedAtMs",now}}}};
  actor.SetPropertyValueDump(lifeKey,state.dump(),false,false);
}

nlohmann::json SrpOriginalLifecycle::RecordAuthoritativeDeath(MpActor& actor,
  const MpActor& killer, const std::string& operationId, double damage, bool dropsResolved)
{
  const auto identity=Identity(actor),fields=actor.GetDynamicFields().GetAsJson();
  Require(!operationId.empty(),"SRP_ORIGINAL_DEATH_OPERATION_REQUIRED");
  Require(actor.IsDead(),"SRP_ORIGINAL_DEATH_NOT_OBSERVED");
  Require(std::isfinite(damage)&&damage>0,"SRP_ORIGINAL_DEATH_DAMAGE_INVALID");
  const nlohmann::json k=killer.GetDynamicFields().GetAsJson();
  const nlohmann::json killerRef={{ "reference",FormDesc::FromFormId(killer.GetFormId(),actor.GetParent()->espmFiles).ToString()},
    {"profileId",std::to_string(killer.GetProfileId())},
    {"characterId",k.value("srpActiveCharacterId",std::string())}};
  nlohmann::json prior=nullptr;
  if(fields.contains(lifeKey)){
    prior=fields.at(lifeKey);
    Require(prior.is_object(),"SRP_ORIGINAL_LIFECYCLE_CORRUPT");
    const int schema=prior.value("schemaVersion",0);
    if(schema==2){
      // Same operation replayed: the durable receipt is the answer, never a
      // second death. A different operation on a dead actor is rejected.
      const auto op=prior.contains("death")?
        prior.at("death").value("operationId",std::string()):std::string();
      if(op==operationId){ ValidateDeath(prior,identity); return prior; }
      throw std::runtime_error("SRP_ORIGINAL_DEATH_ALREADY_RECORDED");
    }
    // Versioned migration: a v1 quarantine receipt is superseded by the
    // authoritative one, and the old receipt is kept for rollback and history.
    Require(schema==1,"SRP_ORIGINAL_LIFECYCLE_CORRUPT");
    ValidateDeath(prior,identity);
  }
  const auto now=std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::system_clock::now().time_since_epoch()).count();
  const nlohmann::json state={{"schemaVersion",2},{"reference",identity.at("reference")},
    {"instanceId",identity.at("instanceId")},{"epoch",0},{"state","DEAD"},
    {"lootEnabled",true},{"xpEnabled",true},{"respawnEnabled",true},
    {"death",{{"operationId",operationId},{"source","SRP_ORIGINAL_IMPACT"},
      {"rewardAuthority",true},{"nativeDropsMaterialized",dropsResolved},
      {"killer",killerRef},{"damage",damage},{"observedAtMs",now}}},
    {"epochs",nlohmann::json::array()},{"supersededQuarantine",prior}};
  ValidateDeath(state,identity);
  actor.SetPropertyValueDump(lifeKey,state.dump(),false,false);
  return state;
}
