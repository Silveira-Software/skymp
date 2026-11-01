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
int64_t Now()
{
  return std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::system_clock::now().time_since_epoch()).count();
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
// A reward flag may only be true once its reward is recorded as delivered;
// otherwise the record advertises loot/XP/respawn that never happened.
bool RewardsBalanced(const nlohmann::json& state)
{
  const auto& rewards=state.contains("rewards")?state.at("rewards"):nlohmann::json();
  const auto done=[&](const char* key){
    return rewards.is_object()&&rewards.value(key,std::string())=="DONE";
  };
  if(state.value("lootEnabled",false)&&!done("loot"))return false;
  if(state.value("xpEnabled",false)&&!done("xp"))return false;
  if(state.value("respawnEnabled",false)&&!done("respawn"))return false;
  return true;
}
void ValidateDeath(const nlohmann::json& state,const nlohmann::json& identity)
{
  Require(state.is_object() &&
    state.value("reference",std::string())==identity.at("reference").get<std::string>() &&
    state.value("instanceId",std::string())==identity.at("instanceId").get<std::string>(),
    "SRP_ORIGINAL_LIFECYCLE_CORRUPT");
  const int schema=state.value("schemaVersion",0);
  if(schema==1){
    // Legacy quarantine receipt: still readable, still refuses rewards, and it
    // is never promoted to an authoritative death without proof of an operation.
    Require(state.value("epoch",-1)==0 && state.value("state",std::string())=="DEAD_QUARANTINED" &&
      state.value("lootEnabled",true)==false && state.value("xpEnabled",true)==false &&
      state.value("respawnEnabled",true)==false && state.contains("death") &&
      state.at("death").is_object(),"SRP_ORIGINAL_LIFECYCLE_CORRUPT");
    const auto& death=state.at("death");
    Require(death.value("operationId",std::string())==identity.at("instanceId").get<std::string>()+":death:0" &&
      death.value("source",std::string())=="NATIVE_KILL_OBSERVATION" &&
      death.value("rewardAuthority",true)==false && death.value("nativeDropsMaterialized",false)==true,
      "SRP_ORIGINAL_DEATH_RECEIPT_CORRUPT");
    return;
  }
  Require(schema==2,"SRP_ORIGINAL_LIFECYCLE_CORRUPT");
  const auto stage=state.value("state",std::string());
  Require(stage=="DYING"||stage=="DEAD","SRP_ORIGINAL_LIFECYCLE_CORRUPT");
  Require(state.value("epoch",-1)>=0 && RewardsBalanced(state),
    "SRP_ORIGINAL_LIFECYCLE_CORRUPT");
  if(stage=="DYING"){
    Require(state.contains("pendingDeath") && state.at("pendingDeath").is_object() &&
      !state.at("pendingDeath").value("operationId",std::string()).empty() &&
      state.at("pendingDeath").contains("killer") &&
      state.value("lootEnabled",true)==false && state.value("xpEnabled",true)==false &&
      state.value("respawnEnabled",true)==false,"SRP_ORIGINAL_DEATH_REQUEST_CORRUPT");
    return;
  }
  Require(state.contains("death") && state.at("death").is_object() &&
    !state.at("death").value("operationId",std::string()).empty() &&
    state.at("death").value("source",std::string())=="SRP_ORIGINAL_IMPACT",
    "SRP_ORIGINAL_DEATH_RECEIPT_CORRUPT");
}
void VerifyPendingDeath(const nlohmann::json& state,const nlohmann::json& identity)
{
  ValidateDeath(state,identity);
  Require(state.value("state",std::string())=="DYING" && state.contains("pendingDeath"),
    "SRP_ORIGINAL_DEATH_REQUEST_CORRUPT");
}
nlohmann::json PendingDeath(const MpActor& actor,const MpActor& killer,
  const std::string& operationId,double damage)
{
  const nlohmann::json k=killer.GetDynamicFields().GetAsJson();
  return {{"operationId",operationId},{"source","SRP_ORIGINAL_IMPACT"},
    {"killer",{{"reference",FormDesc::FromFormId(killer.GetFormId(),actor.GetParent()->espmFiles).ToString()},
      {"profileId",std::to_string(killer.GetProfileId())},
      {"characterId",k.value("srpActiveCharacterId",std::string())}}},
    {"damage",damage},{"requestedAtMs",Now()}};
}
}

bool SrpOriginalLifecycle::Managed(const MpActor& actor)
{
  const auto& fields=actor.GetDynamicFields().GetAsJson();
  // Corruption must not downgrade a corpse into the unguarded legacy path.
  return fields.contains(originKey)||fields.contains(lifeKey)||
    (actor.GetParent()&&actor.GetParent()->IsSrpOriginalControlled(actor.GetFormId()));
}

nlohmann::json SrpOriginalLifecycle::Inspect(const MpActor& actor)
{
  const auto identity=Identity(actor),fields=actor.GetDynamicFields().GetAsJson();
  const auto state=fields.contains(lifeKey)?fields.at(lifeKey):nlohmann::json();
  if(!state.is_null()){
    ValidateDeath(state,identity);
    if(state.value("state",std::string())=="DEAD")
      Require(actor.IsDead(),"SRP_ORIGINAL_DEATH_STATE_MISMATCH");
  }
  auto* world=actor.GetParent();
  const auto generation=world->GetChangeFormGeneration(actor.GetFormId());
  const auto host=world->hosters.find(actor.GetFormId());
  const auto stage=state.is_null()?std::string():state.value("state",std::string());
  const bool dead=stage=="DEAD";
  return {{"identity",identity},{"lifecycle",state},
    {"state",state.is_null()?(actor.IsDead()?"LEGACY_DEATH_REQUIRES_REVIEW":"ALIVE_OBSERVED"):stage},
    {"generation",generation},{"saved",generation>0&&world->IsChangeFormSaved(actor.GetFormId(),generation)},
    {"hostActorId",host==world->hosters.end()?0:host->second},
    {"hostEvidence","TRANSPORT_OWNER_NOT_COLLISION_OR_REWARD_AUTHORITY"},
    {"lootEnabled",dead&&state.value("lootEnabled",false)},
    {"xpEnabled",dead&&state.value("xpEnabled",false)},
    {"respawnEnabled",dead&&state.value("respawnEnabled",false)}};
}

void SrpOriginalLifecycle::RecordDeath(MpActor& actor,const MpActor* killer)
{
  const auto identity=Identity(actor),fields=actor.GetDynamicFields().GetAsJson();
  Require(actor.IsDead(),"SRP_ORIGINAL_DEATH_NOT_OBSERVED");
  if(fields.contains(lifeKey)){
    const auto state=fields.at(lifeKey);
    Require(state.is_object(),"SRP_ORIGINAL_LIFECYCLE_CORRUPT");
    // Authoritative death: the pending request is finalised inside the same
    // transaction that wrote the corpse and its drops, so a crash can leave a
    // request pending but never a corpse without its receipt.
    if(state.value("schemaVersion",0)==2&&state.value("state",std::string())=="DYING"){
      VerifyPendingDeath(state,identity);
      const auto& pending=state.at("pendingDeath");
      nlohmann::json final={{"schemaVersion",2},{"reference",identity.at("reference")},
        {"instanceId",identity.at("instanceId")},{"epoch",state.value("epoch",0)},
        {"state","DEAD"},{"lootEnabled",false},{"xpEnabled",false},{"respawnEnabled",false},
        {"death",{{"operationId",pending.at("operationId")},{"source","SRP_ORIGINAL_IMPACT"},
          {"rewardAuthority",false},{"nativeDropsMaterialized",true},
          {"killer",pending.at("killer")},{"damage",pending.at("damage")},
          {"observedAtMs",Now()}}},
        {"rewards",{{"loot","PENDING"},{"xp","PENDING"},{"respawn","PENDING"}}},
        {"epochs",nlohmann::json::array()}};
      ValidateDeath(final,identity);
      actor.SetPropertyValueDump(lifeKey,final.dump(),false,false);
      return;
    }
    throw std::runtime_error("SRP_ORIGINAL_DEATH_ALREADY_RECORDED");
  }
  nlohmann::json origin=nullptr;
  if(killer){
    const auto k=killer->GetDynamicFields().GetAsJson();
    origin={{"reference",FormDesc::FromFormId(killer->GetFormId(),actor.GetParent()->espmFiles).ToString()},
      {"profileId",std::to_string(killer->GetProfileId())},
      {"characterId",k.value("srpActiveCharacterId",std::string())}};
  }
  const nlohmann::json state={{"schemaVersion",1},{"reference",identity.at("reference")},
    {"instanceId",identity.at("instanceId")},{"epoch",0},{"state","DEAD_QUARANTINED"},
    {"lootEnabled",false},{"xpEnabled",false},{"respawnEnabled",false},
    {"death",{{"operationId",identity.at("instanceId").get<std::string>()+":death:0"},
      {"source","NATIVE_KILL_OBSERVATION"},{"rewardAuthority",false},
      {"nativeDropsMaterialized",true},{"killer",origin},{"observedAtMs",Now()}}}};
  actor.SetPropertyValueDump(lifeKey,state.dump(),false,false);
}

nlohmann::json SrpOriginalLifecycle::BeginAuthoritativeDeath(MpActor& actor,
  const MpActor& killer,const std::string& operationId,double damage)
{
  const auto identity=Identity(actor),fields=actor.GetDynamicFields().GetAsJson();
  Require(!operationId.empty(),"SRP_ORIGINAL_DEATH_OPERATION_REQUIRED");
  Require(std::isfinite(damage)&&damage>0,"SRP_ORIGINAL_DEATH_DAMAGE_INVALID");
  if(fields.contains(lifeKey)){
    const auto existing=fields.at(lifeKey);
    Require(existing.is_object(),"SRP_ORIGINAL_LIFECYCLE_CORRUPT");
    ValidateDeath(existing,identity);
    const auto stage=existing.value("state",std::string());
    const auto& held=stage=="DYING"?existing.at("pendingDeath"):existing.at("death");
    Require(held.value("operationId",std::string())==operationId,
      "SRP_ORIGINAL_DEATH_ALREADY_RECORDED");
    // Same operation: the request is already durable; the corpse step may run
    // again and will stay idempotent.
    return existing;
  }
  const nlohmann::json state={{"schemaVersion",2},{"reference",identity.at("reference")},
    {"instanceId",identity.at("instanceId")},{"epoch",0},{"state","DYING"},
    {"lootEnabled",false},{"xpEnabled",false},{"respawnEnabled",false},
    {"pendingDeath",PendingDeath(actor,killer,operationId,damage)}};
  ValidateDeath(state,identity);
  actor.SetPropertyValueDump(lifeKey,state.dump(),false,false);
  return state;
}

bool SrpOriginalLifecycle::AwaitingCorpse(const MpActor& actor)
{
  const auto& fields=actor.GetDynamicFields().GetAsJson();
  if(!fields.contains(lifeKey))return false;
  const auto state=fields.at(lifeKey);
  return state.is_object()&&state.value("schemaVersion",0)==2&&
    state.value("state",std::string())=="DYING";
}
