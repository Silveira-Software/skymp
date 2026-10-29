#include "SrpOriginalReference.h"
#include "MpActor.h"
#include "WorldState.h"
#include "libespm/GroupUtils.h"
#include <cmath>
#include <iomanip>
#include <random>
#include <sstream>
#include <stdexcept>

namespace {
constexpr auto marker = "private.srpOriginalReferenceV1";
void Require(bool condition, const char* error)
{
  if (!condition) throw std::runtime_error(error);
}
bool Token(const nlohmann::json& value)
{
  if (!value.is_string()) return false;
  const auto s = value.get<std::string>();
  return !s.empty() && s.size() <= 128 && s.find_first_not_of(
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._:-") == std::string::npos;
}
std::string NewIdentity()
{
  std::random_device rng;
  std::ostringstream out;
  out << std::hex << std::setfill('0');
  for (int i = 0; i < 4; ++i) out << std::setw(8) << rng();
  return out.str();
}
}

nlohmann::json SrpOriginalReference::Adopt(MpActor& actor, const nlohmann::json& policy)
{
  auto* world = actor.GetParent();
  Require(world && actor.GetFormId() < 0xff000000, "SRP_ORIGINAL_STATIC_REFERENCE_REQUIRED");
  const auto fields = actor.GetDynamicFields().GetAsJson();
  Require(!fields.contains("private.srpOriginalLifecycleV1") || fields.contains(marker),
    "SRP_ORIGINAL_LIFECYCLE_ORPHANED");
  Require(!actor.IsCreatedAsPlayer() && actor.GetProfileId() < 0 &&
    (!fields.contains("srpActiveCharacterId") || fields.at("srpActiveCharacterId").is_null() ||
     fields.at("srpActiveCharacterId") == ""), "SRP_ORIGINAL_PLAYER_FORBIDDEN");
  Require(!actor.IsDeleted() && !actor.IsDisabled(), "SRP_ORIGINAL_INACTIVE");
  Require(policy.is_object() && policy.size() == 2 && policy.contains("speciesId") &&
    policy.contains("policyRevision") && Token(policy.at("speciesId")) &&
    Token(policy.at("policyRevision")), "SRP_ORIGINAL_POLICY_INVALID");
  auto& browser = world->GetEspm().GetBrowser();
  auto& cache = world->GetEspmCache();
  const auto original = browser.LookupById(actor.GetFormId());
  Require(original.rec && original.rec->GetType() == "ACHR", "SRP_ORIGINAL_ACHR_REQUIRED");
  Require(!(original.rec->GetFlags() & (0x20 | 0x800 | 0x200)), "SRP_ORIGINAL_RECORD_INACTIVE");
  const auto data = reinterpret_cast<const espm::ACHR*>(original.rec)->GetData(cache);
  const auto baseId = original.ToGlobalId(data.baseId);
  const auto base = browser.LookupById(baseId);
  Require(base.rec && base.rec->GetType() == "NPC_" && baseId == actor.GetBaseId(),
    "SRP_ORIGINAL_BASE_MISMATCH");
  const auto location = original.ToGlobalId(espm::GetWorldOrCell(browser, original.rec));
  Require(location && actor.GetCellOrWorld().ToFormId(world->espmFiles) == location,
    "SRP_ORIGINAL_LOCATION_MISMATCH");
  Require(data.loc && std::isfinite(data.loc->pos[0]) && std::isfinite(data.loc->pos[1]) &&
    std::isfinite(data.loc->pos[2]), "SRP_ORIGINAL_POSITION_INVALID");
  auto desc = [&](uint32_t id) { return FormDesc::FromFormId(id, world->espmFiles).ToString(); };
  nlohmann::json expected = {
    {"schemaVersion", 1}, {"reference", desc(actor.GetFormId())},
    {"base", desc(baseId)}, {"worldOrCellDesc", desc(location)},
    {"originalPosition", {data.loc->pos[0], data.loc->pos[1], data.loc->pos[2]}},
    {"speciesId", policy.at("speciesId")}, {"policyRevision", policy.at("policyRevision")},
    {"lifecycleEpoch", 0}, {"rewardsEnabled", false}
  };
  auto receipt = expected;
  const bool duplicate = fields.contains(marker);
  if (duplicate) {
    receipt = fields.at(marker);
    Require(receipt.is_object() && receipt.size() == expected.size() + 1 &&
      receipt.contains("instanceId") && Token(receipt.at("instanceId")), "SRP_ORIGINAL_RECEIPT_CORRUPT");
    for (auto it = expected.begin(); it != expected.end(); ++it)
      Require(receipt.contains(it.key()) && receipt.at(it.key()) == it.value(),
        "SRP_ORIGINAL_RECEIPT_CONFLICT");
  } else {
    // Do not adopt a dead/respawning actor into a fresh reward life.
    Require(!actor.IsDead() && !actor.IsRespawning(), "SRP_ORIGINAL_LIFECYCLE_UNRESOLVED");
    receipt["instanceId"] = NewIdentity();
  }
  auto generation = world->GetChangeFormGeneration(actor.GetFormId());
  // Generation zero after cold load is NOT an ACK. Reassert the SAME identity.
  if (!duplicate || generation == 0) {
    Require(!world->IsFormLoadingInProgress(), "SRP_ORIGINAL_SAVE_DURING_LOAD_FORBIDDEN");
    const auto previousGeneration = generation;
    actor.RunChangeFormTransaction([&] {
      actor.SetPropertyValueDump(marker, receipt.dump(), false, false);
      return true;
    });
    generation = world->GetChangeFormGeneration(actor.GetFormId());
    Require(generation > previousGeneration, "SRP_ORIGINAL_SAVE_NOT_REQUESTED");
  }
  return {{"receipt", receipt}, {"generation", generation}, {"duplicate", duplicate},
    {"saved", generation > 0 && world->IsChangeFormSaved(actor.GetFormId(), generation)}};
}
