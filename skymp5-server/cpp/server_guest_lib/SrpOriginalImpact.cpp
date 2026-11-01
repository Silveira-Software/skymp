#include "SrpOriginalImpact.h"
#include "HitData.h"
#include "MpActor.h"
#include "PartOne.h"
#include "ServerState.h"
#include "SrpActorStats.h"
#include "SrpOriginalLifecycle.h"
#include "SrpOriginalStats.h"
#include "WorldState.h"
#include "libespm/espm.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <sodium.h>
#include <stdexcept>

namespace SrpOriginalImpact {
namespace {
using Json = nlohmann::json;
using Vec = SrpCollision::Vec3;
constexpr size_t capacity = 4096;
constexpr int64_t lifetimeMs = 5000;
void Require(bool accepted, const char* message)
{
  if (!accepted) throw std::runtime_error(message);
}
bool Token(const std::string& s)
{
  return !s.empty() && s.size() <= 128 && s.find_first_not_of(
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._:-") == std::string::npos;
}
std::string NewId()
{
  unsigned char bytes[32]; char out[65];
  randombytes_buf(bytes,sizeof(bytes));
  sodium_bin2hex(out,sizeof(out),bytes,sizeof(bytes));return out;
}
std::string Digest(Json value)
{
  if(value.is_object())value.erase("checksum");
  const auto text=value.dump();
  unsigned char bytes[crypto_hash_sha256_BYTES];char out[crypto_hash_sha256_BYTES*2+1];
  crypto_hash_sha256(bytes,reinterpret_cast<const unsigned char*>(text.data()),text.size());
  sodium_bin2hex(out,sizeof(out),bytes,sizeof(bytes));return out;
}
Json Seal(Json value) { value["checksum"]=Digest(value);return value; }
void Checksum(const Json& value,const char* error)
{
  Require(value.is_object() && value.value("checksum",std::string())==Digest(value),error);
}
int64_t Now()
{
  return std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::system_clock::now().time_since_epoch()).count();
}
bool Saved(const MpActor& actor)
{
  auto* world=actor.GetParent();const auto generation=world->GetChangeFormGeneration(actor.GetFormId());
  return generation && world->IsChangeFormSaved(actor.GetFormId(),generation);
}
MpActor& Loaded(WorldState& world,uint32_t id)
{
  const auto form=world.LookupFormByIdNoLoad(id);auto* actor=form?form->AsActor():nullptr;
  Require(actor,"SRP_IMPACT_ACTOR_NOT_LOADED");return *actor;
}
MpActor& Authenticated(PartOne& server,Networking::UserId user)
{
  Require(server.serverState.IsConnected(user) &&
    !server.serverState.SessionIncarnation(user).empty(),"SRP_IMPACT_SESSION_REQUIRED");
  auto* actor=server.serverState.ActorByUser(user);
  Require(actor && actor->GetParent()==&server.worldState,"SRP_IMPACT_AUTHENTICATED_ACTOR_REQUIRED");
  Require(!SrpOriginalLifecycle::Managed(*actor),"SRP_IMPACT_NATIVE_NPC_ATTACK_PRODUCER_REQUIRED");
  return *actor;
}
void Active(const MpActor& actor)
{
  Require(!actor.IsDead() && !actor.IsRespawning() && !actor.IsDisabled() && !actor.IsDeleted(),
    "SRP_IMPACT_ACTOR_INACTIVE");
}
Json PlayerIdentity(const MpActor& actor,bool allowMissing=false)
{
  const auto& fields=actor.GetDynamicFields().GetAsJson();
  Require(actor.GetProfileId()>0 && fields.contains("srpActiveCharacterId") &&
    fields.at("srpActiveCharacterId").is_string() &&
    Token(fields.at("srpActiveCharacterId").get<std::string>()),"SRP_IMPACT_CHARACTER_REQUIRED");
  Json expected={{"schemaVersion",1},{"reference",FormDesc::FromFormId(actor.GetFormId(),actor.GetParent()->espmFiles).ToString()},
    {"profileId",std::to_string(actor.GetProfileId())},{"characterId",fields.at("srpActiveCharacterId")}};
  if(!fields.contains(kIdentityProperty)){
    Require(allowMissing,"SRP_IMPACT_PERSISTENT_IDENTITY_REQUIRED");return expected;
  }
  const auto identity=fields.at(kIdentityProperty);
  Checksum(identity,"SRP_IMPACT_IDENTITY_CORRUPT");
  Require(identity.value("incarnation",std::string()).size()==64,"SRP_IMPACT_IDENTITY_CORRUPT");
  auto comparable=identity;comparable.erase("checksum");comparable.erase("incarnation");
  Require(comparable==expected,"SRP_IMPACT_CHARACTER_IDENTITY_MISMATCH");return identity;
}
Json TargetIdentity(const MpActor& actor)
{
  Require(SrpOriginalLifecycle::Managed(actor),"SRP_IMPACT_ORIGINAL_TARGET_REQUIRED");
  return SrpOriginalLifecycle::Inspect(actor).at("identity");
}
Json Ledger(const MpActor& actor,const char* kind,const Json& identity)
{
  const auto& fields=actor.GetDynamicFields().GetAsJson();
  if(!fields.contains(kProperty))return {{"schemaVersion",1},{"kind",kind},{"owner",identity},
    {"pending",nullptr},{"requests",Json::object()},{"operations",Json::object()}};
  const auto ledger=fields.at(kProperty);Checksum(ledger,"SRP_IMPACT_LEDGER_CORRUPT");
  Require(ledger.value("schemaVersion",0)==1 && ledger.value("kind",std::string())==kind &&
    ledger.at("owner")==identity && ledger.at("operations").is_object() &&
    ledger.at("requests").is_object() && ledger.at("operations").size()<=capacity &&
    ledger.at("requests").size()<=capacity,"SRP_IMPACT_LEDGER_CORRUPT");
  for(const auto& [id,entry]:ledger.at("operations").items()){
    Checksum(entry,"SRP_IMPACT_RECEIPT_CORRUPT");
    Require(Token(id) && entry.at("operationId")==id,"SRP_IMPACT_RECEIPT_CORRUPT");
    if(std::string(kind)=="OUTBOX"){
      const auto& intent=entry.at("intent");Checksum(intent,"SRP_IMPACT_INTENT_CORRUPT");
      Require(intent.at("attacker")==identity && intent.at("operationId")==id &&
        Token(intent.at("requestId").get<std::string>()) &&
        (entry.at("state")=="ISSUED" || entry.at("state")=="APPLIED" || entry.at("state")=="CANCELLED"),
        "SRP_IMPACT_INTENT_CORRUPT");
      Require(ledger.at("requests").contains(intent.at("requestId").get<std::string>()) &&
        ledger.at("requests").at(intent.at("requestId").get<std::string>())==id,
        "SRP_IMPACT_REQUEST_INDEX_CORRUPT");
    }else Require(entry.at("target")==identity && entry.at("state")=="APPLIED",
      "SRP_IMPACT_RECEIPT_CORRUPT");
  }
  for(const auto& [request,id]:ledger.at("requests").items())
    Require(Token(request) && id.is_string() && ledger.at("operations").contains(id.get<std::string>()),
      "SRP_IMPACT_REQUEST_INDEX_CORRUPT");
  if(!ledger.at("pending").is_null())Require(ledger.at("pending").is_string() &&
    ledger.at("operations").contains(ledger.at("pending").get<std::string>()) &&
    ledger.at("operations").at(ledger.at("pending").get<std::string>()).at("state")=="ISSUED",
    "SRP_IMPACT_PENDING_CORRUPT");
  return ledger;
}
Json Host(PartOne& server,const MpActor& target)
{
  auto it=server.worldState.hosters.find(target.GetFormId());
  if(it==server.worldState.hosters.end())return {{"actorId",0},{"session",nullptr}};
  auto& actor=Loaded(server.worldState,it->second);
  const auto user=server.serverState.UserByActor(&actor);
  Require(server.serverState.IsConnected(user) && !server.serverState.SessionIncarnation(user).empty(),
    "SRP_IMPACT_TARGET_HOST_SESSION_REQUIRED");
  return {{"actorId",actor.GetFormId()},{"userId",user},
    {"profileId",actor.GetProfileId()},{"characterId",actor.GetDynamicFields().GetAsJson().value("srpActiveCharacterId",std::string())},
    {"session",server.serverState.SessionIncarnation(user)}};
}
struct Weapon { uint32_t source=0x1f4; double reach=0; int64_t interval=1000; };
Weapon NativeWeapon(const MpActor& actor)
{
  auto* world=actor.GetParent();const auto& equipment=actor.GetEquipment();Weapon result;
  Require(!equipment.rightSpell && !equipment.leftSpell,"SRP_IMPACT_MELEE_WEAPON_REQUIRED");
  size_t count=0;
  for(const auto& entry:equipment.inv.entries){
    if(entry.GetWorn()==Inventory::Worn::None)continue;
    const auto lookup=world->GetEspm().GetBrowser().LookupById(entry.baseId);
    Require(lookup.rec,"SRP_IMPACT_EQUIPMENT_RECORD_MISSING");
    if(lookup.rec->GetType()!=espm::WEAP::kType)continue;
    Require(entry.GetWorn()==Inventory::Worn::Right && entry.count==1 &&
      actor.GetInventory().GetItemCount(entry.baseId)>0,"SRP_IMPACT_RIGHT_HAND_WEAPON_REQUIRED");
    result.source=entry.baseId;++count;
  }
  Require(count<=1,"SRP_IMPACT_SINGLE_WEAPON_REQUIRED");
  if(!count){
    result.reach=espm::GetData<espm::RACE>(actor.GetRaceId(),world).unarmedReach;
  }else{
    const auto record=espm::GetData<espm::WEAP>(result.source,world);
    Require(record.weapDNAM && record.weapData,"SRP_IMPACT_WEAPON_RECORD_INCOMPLETE");
    const auto& data=*record.weapDNAM;
    Require(data.animType>=espm::WEAP::AnimType::OneHandSword &&
      data.animType<=espm::WEAP::AnimType::TwoHandAxe,"SRP_IMPACT_MELEE_ONLY");
    const auto distance=espm::GetData<espm::GMST>(espm::GMST::kFCombatDistance,world).value;
    Require(std::isfinite(data.speed) && data.speed>0 && data.speed<=10 &&
      std::isfinite(distance) && distance>0,"SRP_IMPACT_WEAPON_RECORD_INVALID");
    result.reach=data.reach*distance;
    // Explicit normal-melee server policy; no animation packet proves a swing.
    result.interval=static_cast<int64_t>(std::ceil(1100.0/data.speed));
  }
  Require(std::isfinite(result.reach) && result.reach>0 && result.reach<=1024,
    "SRP_IMPACT_REACH_INVALID");return result;
}
Json WeaponJson(const Weapon& weapon,const MpActor& actor)
{
  return {{"source",weapon.source},{"reach",weapon.reach},{"intervalMs",weapon.interval},
    {"equipmentDigest",Digest(actor.GetEquipment().ToJson())},
    {"policy","NORMAL_MELEE_SEGMENT_V1"}};
}
Vec Sub(Vec a,Vec b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
Vec Cross(Vec a,Vec b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
double Dot(Vec a,Vec b){return a.x*b.x+a.y*b.y+a.z*b.z;}
bool Finite(Vec a){return std::isfinite(a.x)&&std::isfinite(a.y)&&std::isfinite(a.z)&&
  std::abs(a.x)<=1e8&&std::abs(a.y)<=1e8&&std::abs(a.z)<=1e8;}
bool Intersection(const SrpCollision::Triangle& t,Vec from,Vec to,double& fraction)
{
  Require(Finite(t.a)&&Finite(t.b)&&Finite(t.c),"SRP_IMPACT_TARGET_MESH_INVALID");
  const auto e1=Sub(t.b,t.a),e2=Sub(t.c,t.a),direction=Sub(to,from),p=Cross(direction,e2);
  const auto n=Cross(e1,e2);Require(Dot(n,n)>1e-18,"SRP_IMPACT_TARGET_MESH_INVALID");
  const double determinant=Dot(e1,p);
  // A coplanar/degenerate attack cannot positively establish target contact.
  if(std::abs(determinant)<1e-9)return false;
  const auto offset=Sub(from,t.a);const double u=Dot(offset,p)/determinant;
  if(u<0||u>1)return false;
  const auto q=Cross(offset,e1);const double v=Dot(direction,q)/determinant;
  if(v<0||u+v>1)return false;
  fraction=Dot(e2,q)/determinant;return fraction>1e-6&&fraction<=1;
}
Json Vector(const NiPoint3& v){return Json::array({v.x,v.y,v.z});}
Json Vector(Vec v){return Json::array({v.x,v.y,v.z});}
Json FrameIdentity(const Frame& frame,double gripHeight)
{
  Require(frame.scene && frame.revision && frame.worldOrCell &&
    frame.movementPolicy=="SERVER_BOUNDED_MOVEMENT_V1" &&
    frame.hitRepresentation=="SERVER_MESH_PROXY_V1" &&
    frame.dynamics.complete && frame.dynamics.worldOrCell==frame.worldOrCell &&
    frame.dynamics.revision && !frame.targetTriangles.empty() && frame.targetTriangles.size()<=100000,
    "SRP_IMPACT_COMPLETE_SERVER_FRAME_REQUIRED");
  Json mesh=Json::array();for(const auto& triangle:frame.targetTriangles)
    mesh.push_back(Json::array({Vector(triangle.a),Vector(triangle.b),Vector(triangle.c)}));
  return {{"revision",frame.revision},{"sceneId",frame.scene->Id()},
    {"dynamicRevision",frame.dynamics.revision},{"worldOrCell",frame.worldOrCell},
    {"movementPolicy",frame.movementPolicy},{"hitRepresentation",frame.hitRepresentation},
    {"attackerPosition",Vector(frame.attackerPosition)},{"attackerAngle",Vector(frame.attackerAngle)},
    {"targetPosition",Vector(frame.targetPosition)},{"targetAngle",Vector(frame.targetAngle)},
    {"targetMeshDigest",Digest(mesh)},{"gripHeight",gripHeight}};
}
// The grip height is read from the attacker's own collision proxy instead of a
// fixed offset. A missing or incoherent proxy refuses the attack: the server
// must never invent a reach origin for an actor it cannot place.
std::optional<double> AttackerGripHeight(const Frame& frame,const MpActor& attacker)
{
  auto* world=attacker.GetParent();
  if(!world)return {};
  const auto baseDesc=FormDesc::FromFormId(attacker.GetBaseId(),world->espmFiles).ToString();
  const auto raceDesc=FormDesc::FromFormId(attacker.GetRaceId(),world->espmFiles).ToString();
  const auto pos=attacker.GetPos(),angle=attacker.GetAngle();
  if(!Finite(Vec{pos.x,pos.y,pos.z})||!std::isfinite(angle.z))return {};
  const double yaw=angle.z*std::acos(-1.0)/180.0,c=std::cos(yaw),s=std::sin(yaw);
  std::array<double,12> pose={c,-s,0.0,pos.x, s,c,0.0,pos.y, 0.0,0.0,1.0,pos.z};
  const auto mesh=frame.scene->ActorTriangles(baseDesc,raceDesc,pose);
  if(!mesh||mesh->empty())return {};
  double minZ=1e300,maxZ=-1e300,sumX=0,sumY=0;size_t count=0;
  for(const auto& triangle:*mesh)for(const auto& v:{triangle.a,triangle.b,triangle.c}){
    minZ=std::min(minZ,v.z);maxZ=std::max(maxZ,v.z);sumX+=v.x;sumY+=v.y;++count;
  }
  if(!count)return {};
  const double height=maxZ-minZ;
  // A proxy thinner than a hand or taller than a giant cannot describe an
  // attacker, and a proxy centred far from the accepted position is desynced.
  if(!(height>=16.0&&height<=512.0))return {};
  if(std::hypot(sumX/count-pos.x,sumY/count-pos.y)>256.0)return {};
  const double grip=minZ+height*0.55;
  return std::isfinite(grip)?std::optional<double>(grip):std::nullopt;
}
Json Contact(const Frame& frame,const Weapon& weapon,double gripHeight)
{
  const auto identity=FrameIdentity(frame,gripHeight);
  // The segment starts at the attacker proxy's grip height and runs along the
  // accepted facing. Neither origin, direction nor hit point is supplied by an
  // impact report, and no engine animation pose is claimed.
  const double yaw=frame.attackerAngle.z*std::acos(-1.0)/180.0;
  Vec from{frame.attackerPosition.x,frame.attackerPosition.y,gripHeight};
  Vec to{from.x+std::sin(yaw)*weapon.reach,from.y+std::cos(yaw)*weapon.reach,from.z};
  Require(Finite(from)&&Finite(to),"SRP_IMPACT_ATTACK_TRANSFORM_INVALID");
  double nearest=2;
  for(const auto& triangle:frame.targetTriangles){double fraction;
    if(Intersection(triangle,from,to,fraction))nearest=std::min(nearest,fraction);
  }
  Require(nearest<=1,"SRP_IMPACT_SERVER_SEGMENT_MISSES_TARGET");
  Vec contact{from.x+(to.x-from.x)*nearest,from.y+(to.y-from.y)*nearest,from.z+(to.z-from.z)*nearest};
  SrpCollision::Query query;query.worldOrCell=frame.worldOrCell;query.from=from;
  query.to=contact;query.dynamics=frame.dynamics;
  const auto trace=frame.scene->Trace(query);
  Require(trace.verdict==SrpCollision::Verdict::Clear,"SRP_IMPACT_SERVER_COLLISION_NOT_CLEAR");
  return {{"frame",identity},{"from",Vector(from)},{"contact",Vector(contact)},
    {"targetFraction",nearest},{"collision","CLEAR"},{"enginePoseParity",false}};
}
Frame Capture(WorldState& world,const MpActor& attacker,const MpActor& target)
{
  auto frame=world.CaptureSrpOriginalImpactFrame(attacker,target);
  Require(frame.has_value(),"SRP_IMPACT_SERVER_FRAME_UNAVAILABLE");return *frame;
}
void Write(MpActor& actor,const Json& ledger)
{
  actor.SetPropertyValueDump(kProperty,Seal(ledger).dump(),false,false);
}
Json Response(const MpActor& actor,const Json& entry,bool duplicate)
{
  return {{"operationId",entry.at("operationId")},{"state",entry.at("state")},
    {"entry",entry},{"duplicate",duplicate},{"saved",Saved(actor)},
    {"generation",actor.GetParent()->GetChangeFormGeneration(actor.GetFormId())},
    {"enginePoseParity",false},{"lootEnabled",false},{"xpEnabled",false}};
}
Json Reconcile(MpActor& attacker,MpActor& target,Json ledger,Json entry,const Json& receipt)
{
  Require(receipt.at("intent")==entry.at("intent"),"SRP_IMPACT_RECEIPT_INTENT_CONFLICT");
  // An image restored from storage has no in-process save generation. Re-save
  // its SAME receipt/health before acknowledging; never repeat the damage.
  if(target.GetParent()->GetChangeFormGeneration(target.GetFormId())==0){
    const auto persisted=target.GetDynamicFields().GetAsJson().at(kProperty);
    target.RunChangeFormTransaction([&]{target.SetPropertyValueDump(kProperty,persisted.dump(),false,false);return true;});
  }
  if(!Saved(target)){
    auto response=Response(attacker,entry,true);response["state"]="TARGET_SAVE_PENDING";
    response["targetReceipt"]=receipt;response["saved"]=false;return response;
  }
  entry["state"]="APPLIED";entry["targetReceipt"]=receipt;entry=Seal(entry);
  ledger["operations"][entry.at("operationId").get<std::string>()]=entry;ledger["pending"]=nullptr;
  attacker.RunChangeFormTransaction([&]{Write(attacker,ledger);return true;});
  target.NetSendChangeValues(target.GetActorValues(),std::vector<espm::ActorValue>{espm::ActorValue::Health});
  return Response(attacker,entry,true);
}
}

Json Begin(PartOne& server,Networking::UserId userId,uint32_t targetId,const std::string& requestId)
{
  Require(Token(requestId),"SRP_IMPACT_REQUEST_ID_INVALID");
  auto& attacker=Authenticated(server,userId);auto identity=PlayerIdentity(attacker,true);
  const bool hasIdentity=identity.contains("incarnation");
  auto ledger=Ledger(attacker,"OUTBOX",identity);
  if(ledger.at("requests").contains(requestId)){
    const auto& entry=ledger.at("operations").at(ledger.at("requests").at(requestId).get<std::string>());
    Require(entry.at("intent").at("targetId")==targetId,"SRP_IMPACT_REQUEST_ID_CONFLICT");
    return Response(attacker,entry,true);
  }
  Require(ledger.at("pending").is_null(),"SRP_IMPACT_PENDING_SWING_EXISTS");
  Require(ledger.at("operations").size()<capacity,"SRP_IMPACT_CAPACITY_REQUIRES_RECONCILIATION");
  auto& target=Loaded(server.worldState,targetId);Require(&target!=&attacker,"SRP_IMPACT_SELF_FORBIDDEN");
  Active(attacker);Active(target);const auto targetIdentity=TargetIdentity(target);
  Require(Saved(attacker)&&Saved(target),"SRP_IMPACT_PRIOR_SAVE_PENDING");
  Require(SrpActorStats::Read(attacker).enabled,"SRP_IMPACT_PLAYER_STATS_REQUIRED");
  Require(SrpOriginalStats::Read(target).has_value(),"SRP_IMPACT_ORIGINAL_STATS_REQUIRED");
  const auto weapon=NativeWeapon(attacker);const auto now=Now();
  for(const auto& [id,prior]:ledger.at("operations").items()){
    const auto issued=prior.at("intent").at("issuedAtMs").get<int64_t>();
    const auto interval=prior.at("intent").at("weapon").at("intervalMs").get<int64_t>();
    Require(now>=issued && now-issued>=interval,"SRP_IMPACT_ATTACK_RATE_LIMIT");
  }
  const auto frame=Capture(server.worldState,attacker,target);
  const auto grip=AttackerGripHeight(frame,attacker);
  Require(grip.has_value(),"SRP_IMPACT_ATTACKER_PROXY_REQUIRED");
  const auto proof=Contact(frame,weapon,*grip),host=Host(server,target);
  // Identity is created only after all admission checks, in the same durable
  // outbox transaction. A denied request cannot mint a combat incarnation.
  if(!hasIdentity){identity["incarnation"]=NewId();identity=Seal(identity);ledger["owner"]=identity;}
  const auto before=server.worldState.GetChangeFormGeneration(attacker.GetFormId());
  Require(before<std::numeric_limits<uint64_t>::max(),"SRP_IMPACT_GENERATION_OVERFLOW");
  const auto operationId=NewId();
  const auto intent=Seal({{"schemaVersion",1},{"operationId",operationId},{"requestId",requestId},
    {"attacker",identity},{"target",targetIdentity},{"targetId",targetId},
    {"attackerGeneration",before+1},{"targetGeneration",server.worldState.GetChangeFormGeneration(targetId)},
    {"session",server.serverState.SessionIncarnation(userId)},{"userId",userId},{"host",host},
    {"issuedAtMs",now},{"expiresAtMs",now+lifetimeMs},{"weapon",WeaponJson(weapon,attacker)},
    {"proof",proof}});
  const auto entry=Seal({{"operationId",operationId},{"state","ISSUED"},{"intent",intent}});
  ledger["operations"][operationId]=entry;ledger["requests"][requestId]=operationId;ledger["pending"]=operationId;
  attacker.RunChangeFormTransaction([&]{
    if(!hasIdentity)attacker.SetPropertyValueDump(kIdentityProperty,identity.dump(),false,false);
    Write(attacker,ledger);return true;
  });
  // Save generations come from one global monotonic counter, so this form's
  // next generation is not necessarily before+1: any other form that requested
  // a save in between consumes a number. What must hold is that this write was
  // accepted and advanced past the generation we captured.
  const auto after=server.worldState.GetChangeFormGeneration(attacker.GetFormId());
  Require(after>before,"SRP_IMPACT_UNEXPECTED_SAVE_GENERATION");
  return Response(attacker,entry,false);
}

Json Apply(PartOne& server,Networking::UserId userId,const std::string& operationId)
{
  Require(Token(operationId),"SRP_IMPACT_OPERATION_ID_INVALID");
  auto& attacker=Authenticated(server,userId);const auto identity=PlayerIdentity(attacker);
  auto ledger=Ledger(attacker,"OUTBOX",identity);
  Require(ledger.at("operations").contains(operationId),"SRP_IMPACT_OPERATION_NOT_ISSUED");
  auto entry=ledger.at("operations").at(operationId);const auto intent=entry.at("intent");
  if(entry.at("state")=="APPLIED")return Response(attacker,entry,true);
  Require(entry.at("state")=="ISSUED" && ledger.at("pending")==operationId,"SRP_IMPACT_OPERATION_NOT_PENDING");
  auto& target=Loaded(server.worldState,intent.at("targetId").get<uint32_t>());
  const auto targetIdentity=TargetIdentity(target);
  Require(targetIdentity==intent.at("target"),"SRP_IMPACT_TARGET_IDENTITY_MISMATCH");
  auto targetLedger=Ledger(target,"TARGET_RECEIPTS",targetIdentity);
  // A durable target-side receipt always wins over replay/reconnect/stale
  // snapshots. Reconciliation can acknowledge it but cannot damage again.
  if(targetLedger.at("operations").contains(operationId)){
    // A previous attempt may have left the death request durable while the corpse
    // transaction never ran. Finish it before acknowledging, so a replay cannot
    // return APPLIED over an unfinished death.
    if(SrpOriginalLifecycle::AwaitingCorpse(target)){
      target.Kill(&attacker,false);
      Require(!SrpOriginalLifecycle::AwaitingCorpse(target),"SRP_IMPACT_DEATH_INCOMPLETE");
    }
    return Reconcile(attacker,target,ledger,entry,targetLedger.at("operations").at(operationId));
  }
  Active(attacker);Active(target);
  Require(intent.at("session")==server.serverState.SessionIncarnation(userId) && intent.at("userId")==userId,
    "SRP_IMPACT_SESSION_CHANGED");
  Require(Host(server,target)==intent.at("host"),"SRP_IMPACT_TARGET_HOST_CHANGED");
  // Save generations come from one global counter, so the attacker's own intent
  // write lands at or past the number its intent recorded (never below it). The
  // target is pinned exactly: any target write between admission and application
  // invalidates the frame the proof was built from.
  const auto attackerGeneration=server.worldState.GetChangeFormGeneration(attacker.GetFormId());
  const auto targetGeneration=server.worldState.GetChangeFormGeneration(target.GetFormId());
  Require(attackerGeneration>=intent.at("attackerGeneration").get<uint64_t>(),
    "SRP_IMPACT_GENERATION_CHANGED");
  Require(targetGeneration==intent.at("targetGeneration").get<uint64_t>(),
    "SRP_IMPACT_GENERATION_CHANGED");
  Require(Saved(attacker)&&Saved(target),"SRP_IMPACT_INTENT_SAVE_PENDING");
  const auto now=Now();Require(now>=intent.at("issuedAtMs").get<int64_t>() &&
    now<=intent.at("expiresAtMs").get<int64_t>(),"SRP_IMPACT_INTENT_EXPIRED");
  const auto weapon=NativeWeapon(attacker);
  Require(WeaponJson(weapon,attacker)==intent.at("weapon"),"SRP_IMPACT_WEAPON_CHANGED");
  const auto frame=Capture(server.worldState,attacker,target);
  const auto grip=AttackerGripHeight(frame,attacker);
  Require(grip.has_value(),"SRP_IMPACT_ATTACKER_PROXY_REQUIRED");
  const auto proof=Contact(frame,weapon,*grip);
  Require(proof==intent.at("proof"),"SRP_IMPACT_FRAME_CHANGED");
  Require(SrpActorStats::Read(attacker).enabled && SrpOriginalStats::Read(target).has_value(),
    "SRP_IMPACT_STATS_REQUIRED");
  Require(targetLedger.at("operations").size()<capacity,"SRP_IMPACT_CAPACITY_REQUIRES_RECONCILIATION");
  HitData hit;hit.aggressor=attacker.GetFormId();hit.target=target.GetFormId();hit.source=weapon.source;
  // The single existing damage formula owns native weapon/armor/SRP scaling.
  // Power, bash, sneak, projectile and client block flags are never copied.
  const float damage=server.CalculateDamage(attacker,target,hit);
  const float maximum=target.GetMaximumValues().health;auto values=target.GetActorValues();
  Require(std::isfinite(damage)&&damage>=0 && std::isfinite(maximum)&&maximum>0 &&
    std::isfinite(values.healthPercentage)&&values.healthPercentage>0&&values.healthPercentage<=1,
    "SRP_IMPACT_RESOURCE_INVALID");
  const float beforeHealth=values.healthPercentage;
  values.healthPercentage=beforeHealth-damage/maximum;
  // Lethal damage is completed here, not deferred: the health write, the engine
  // death state with the corpse drops, and the authoritative life receipt.
  const bool lethal=!(std::isfinite(values.healthPercentage)&&values.healthPercentage>0);
  if(lethal)values.healthPercentage=0.0f;
  const auto receipt=Seal({{"schemaVersion",1},{"operationId",operationId},{"state","APPLIED"},
    {"attacker",identity},{"target",targetIdentity},{"intent",intent},{"proof",proof},
    {"damage",damage},{"maximumHealth",maximum},{"beforeHealth",beforeHealth},
    {"afterHealth",values.healthPercentage},{"lethal",lethal},{"appliedAtMs",now},
    {"enginePoseParity",false},{"lootEnabled",false},{"xpEnabled",false}});
  targetLedger["operations"][operationId]=receipt;
  if(lethal){
    // Lethal cannot write health through SetPercentages: a zero percentage calls
    // Kill, and Kill opens its own ChangeForm transaction. The durable unit is
    // therefore the receipt plus the death request; the corpse transaction then
    // finalises that same request. If the process dies in between, the request
    // stays DYING and the next Apply resumes it instead of repeating the death.
    target.RunChangeFormTransaction([&]{
      Write(target,targetLedger);
      SrpOriginalLifecycle::BeginAuthoritativeDeath(target,attacker,operationId,damage);
      return true;
    });
    target.Kill(&attacker,false);
    Require(!SrpOriginalLifecycle::AwaitingCorpse(target),"SRP_IMPACT_DEATH_INCOMPLETE");
    const auto death=SrpOriginalLifecycle::Inspect(target);
    auto response=Response(attacker,entry,false);
    response["state"]="TARGET_SAVE_PENDING";response["lethal"]=true;
    response["death"]=death;response["targetReceipt"]=receipt;
    response["saved"]=false;return response;
  }
  target.RunChangeFormTransaction([&]{
    target.SetPercentages(values,nullptr);Write(target,targetLedger);return true;
  });
  auto response=Response(attacker,entry,false);response["state"]="TARGET_SAVE_PENDING";
  response["targetReceipt"]=receipt;response["saved"]=false;return response;
}

Json Cancel(PartOne& server,Networking::UserId userId,const std::string& operationId)
{
  Require(Token(operationId),"SRP_IMPACT_OPERATION_ID_INVALID");
  auto& attacker=Authenticated(server,userId);auto ledger=Ledger(attacker,"OUTBOX",PlayerIdentity(attacker));
  Require(ledger.at("operations").contains(operationId),"SRP_IMPACT_OPERATION_NOT_ISSUED");
  auto entry=ledger.at("operations").at(operationId);
  if(entry.at("state")!="ISSUED")return Response(attacker,entry,true);
  auto& target=Loaded(server.worldState,entry.at("intent").at("targetId").get<uint32_t>());
  auto targetLedger=Ledger(target,"TARGET_RECEIPTS",TargetIdentity(target));
  if(targetLedger.at("operations").contains(operationId))
    return Reconcile(attacker,target,ledger,entry,targetLedger.at("operations").at(operationId));
  entry["state"]="CANCELLED";entry=Seal(entry);ledger["operations"][operationId]=entry;ledger["pending"]=nullptr;
  attacker.RunChangeFormTransaction([&]{Write(attacker,ledger);return true;});
  return Response(attacker,entry,false);
}

Json Inspect(const MpActor& actor)
{
  const auto& fields=actor.GetDynamicFields().GetAsJson();
  if(!fields.contains(kProperty))return {{"present",false},{"enginePoseParity",false},
    {"legacyHitAuthority",false},{"lootEnabled",false},{"xpEnabled",false}};
  const bool original=SrpOriginalLifecycle::Managed(actor);
  const auto ledger=Ledger(actor,original?"TARGET_RECEIPTS":"OUTBOX",original?TargetIdentity(actor):PlayerIdentity(actor));
  return {{"present",true},{"ledger",ledger},{"saved",Saved(actor)},
    {"enginePoseParity",false},{"legacyHitAuthority",false},{"lootEnabled",false},{"xpEnabled",false}};
}
}
