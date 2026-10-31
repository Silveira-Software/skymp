#include "WorldState.h"
#include "EvaluateTemplate.h"
#include "FormCallbacks.h"
#include "LeveledListUtils.h"
#include "LocationalDataUtils.h"
#include "MpActor.h"
#include "MpChangeForms.h"
#include "MpObjectReference.h"
#include "SaveGenerationTracker.h"
#include "SrpNpcAdmission.h"
#include "HostStopMessage.h"
#include "libespm/NPC_.h"
#include "libespm/ScriptData.h"
#include "libespm/RecordHeaderAccess.h"
#include "libespm/ReadUnaligned.h"
#include "SrpOriginalLifecycle.h"
#include "SrpOriginalEncounter.h"
#include "gamemode_events/ChangeFormsSavedEvent.h"
#include "libespm/GroupUtils.h"
#include "papyrus-vm/Reader.h"
#include "papyrus-vm/Utils.h"
#include "script_classes/PapyrusClassesFactory.h"
#include "script_compatibility_policies/PapyrusCompatibilityPolicyFactory.h"
#include "script_storages/IScriptStorage.h"
#include <ScopedTask.h>
#include <Timer.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <antigo/Context.h>
#include <deque>
#include <fmt/format.h>
#include <fmt/ranges.h>
#include <iterator>
#include <limits>
#include <optional>
#include <save_storages/AsyncSaveStorage.h> // UpsertFailedException
#include <save_storages/ISaveStorage.h>
#include <tuple>
#include <unordered_map>

namespace {
std::pair<int16_t, int16_t> ColdGridPos(const NiPoint3& pos)
{
  // Same truncation used by the existing grid. Caller validates the range.
  const auto coordinate = [](float v) -> int16_t {
    const double g = double(v) / 4096.0;
    return std::isfinite(g) ? int16_t(std::clamp(g, -32767.0, 32765.0)) : 0;
  };
  return { coordinate(pos.x), coordinate(pos.y) };
}
}

namespace {

struct RelootTimeForTypesEntry
{
  std::string recordType;
  std::chrono::system_clock::duration time;

  Viet::Timer timer;
};
}

struct WorldState::Impl
{
  std::map<uint32_t,std::pair<nlohmann::json,uint64_t>> encounterSaveGenerations;
  SrpNpcAdmission npcAdmission;
  bool liveUnloadEnabled = false;
  uint32_t liveUnloadGraceMs = 30000, maxNpcsPerHost = 64;
  struct Suspension {
    std::chrono::steady_clock::time_point since;
    uint64_t generation = 0;
  };
  std::map<uint32_t, Suspension> suspendingNpcs;
  uint64_t liveUnloads = 0, cancelledUnloads = 0;
  bool questPinsScanned = false;
  std::set<uint32_t> questPinnedForms;
  uint64_t connectionSerial = 0;
  std::map<uint32_t, uint64_t> networkSessions, retiredNetworkIndices;

  bool npcAdmissionDeferred = false;
  bool npcInitializationFailed = false;
  // Existing dynamic NPC images may far exceed active admission budgets.
  // Keep their exact ChangeForms cold; only interest/direct lookup may load.
  std::set<uint32_t> coldNpcIds, loadingColdNpcIds;
  std::set<uint32_t> retiringNpcIds, preparingNpcIds, npcTombstoneIds;
  std::map<std::tuple<uint32_t, int16_t, int16_t>, std::set<uint32_t>>
    coldNpcRegions;
  std::vector<std::optional<MpChangeForm>> changesByIdx;
  bool changesByIdxEmpty = true;

  std::shared_ptr<
    Viet::ISaveStorage<MpChangeForm, FormDesc, std::vector<FormDesc>>>
    saveStorage;
  std::shared_ptr<IScriptStorage> scriptStorage;
  bool saveStorageBusy = false;
  SaveGenerationTracker saveGenerations;
  std::vector<uint64_t> savedBatchesToNotify;
  std::shared_ptr<VirtualMachine> vm;
  uint32_t nextId = 0xff000000;
  std::shared_ptr<IPapyrusCompatibilityPolicy> policy;
  std::unordered_map<uint32_t, MpChangeForm> changeFormsForDeferredLoad;
  bool chunkLoadingInProgress = false;
  bool formLoadingInProgress = false;
  std::vector<RelootTimeForTypesEntry> relootTimeForTypes;
  std::set<std::string> forbiddenRelootTypes;
  std::vector<std::unique_ptr<IPapyrusClassBase>> classes;
  std::array<std::shared_ptr<std::vector<uint32_t>>, 0x100>
    allFormsByModIndexCache;
  std::vector<uint32_t> attachEspmRecordFailures;
};

WorldState::WorldState()
  : worldStartTime(std::chrono::steady_clock::now())
{
  logger.reset(new spdlog::logger("empty logger"));

  pImpl.reset(new Impl);
  pImpl->policy = PapyrusCompatibilityPolicyFactory::Create(this);
}

void WorldState::Clear()
{
  if (pImpl->liveUnloadEnabled && !pImpl->networkSessions.empty())
    throw std::runtime_error("SRP_NPC_LIVE_CLEAR_REQUIRES_NETWORK_DRAIN");
  pImpl->suspendingNpcs.clear();
  pImpl->retiredNetworkIndices.clear();
  pImpl->networkSessions.clear();
  pImpl->encounterSaveGenerations.clear();
  forms.clear();
  grids.clear();
  formIdxManager.reset();
  pImpl->npcAdmission.ResetLeases();
  pImpl->retiringNpcIds.clear();
  pImpl->preparingNpcIds.clear();
  pImpl->npcAdmissionDeferred=false;
}

const std::chrono::steady_clock::time_point& WorldState::GetStartPoint() const
{
  return worldStartTime;
}

void WorldState::AttachEspm(espm::Loader* espm_,
                            const FormCallbacksFactory& formCallbacksFactory_)
{
  espm = espm_;
  formCallbacksFactory = formCallbacksFactory_;
  espmCache.reset(new espm::CompressedFieldsCache);
  espmFiles = espm->GetFileNames();
}

void WorldState::AttachSaveStorage(
  std::shared_ptr<
    Viet::ISaveStorage<MpChangeForm, FormDesc, std::vector<FormDesc>>>
    saveStorage)
{
  spdlog::info("AttachSaveStorage - db fixes installed");

  pImpl->saveStorage = saveStorage;
}

void WorldState::AttachScriptStorage(
  std::shared_ptr<IScriptStorage> scriptStorage)
{
  pImpl->scriptStorage = scriptStorage;
}

void WorldState::AddForm(std::unique_ptr<MpForm> form, uint32_t formId,
                         bool skipChecks,
                         const MpChangeForm* optionalChangeFormToApply)
{
  if (pImpl->npcInitializationFailed)
    throw std::runtime_error("SRP_NPC_INITIALIZATION_FAILED_RESTART_REQUIRED");
  if (forms.find(formId) != forms.end()) {
    if (skipChecks)
      return; // Keep the existing original reference untouched.
    throw std::runtime_error(
      fmt::format("Form with id {:x} already exists", formId));
  }

  if (pImpl->npcTombstoneIds.count(formId) ||
      (pImpl->coldNpcIds.count(formId) &&
       !pImpl->loadingColdNpcIds.count(formId)))
    throw std::runtime_error("SRP_COLD_NPC_ID_RESERVED");

  // Reserve identity before Init can materialize items, scripts or grid state.
  // The same path counts original and dynamic NPCs; a player never consumes a
  // fauna/creature slot. Saved static state determines current occupancy.
  if (auto* actor = form->AsActor(); actor && pImpl->npcAdmission.Enabled()) {
    const MpChangeForm* saved = optionalChangeFormToApply;
    if (!saved) {
      auto it = pImpl->changeFormsForDeferredLoad.find(formId);
      if (it != pImpl->changeFormsForDeferredLoad.end())
        saved = &it->second;
    }
    const bool player = formId >= 0xff000000 &&
      (actor->GetBaseId() <= 7 || (saved && saved->profileId >= 0));
    const auto location =
      saved ? saved->worldOrCellDesc : actor->GetCellOrWorld();
    const auto pos = saved ? saved->position : actor->GetPos();
    // Exempt legacy/player actors before evaluating any NPC region/source.
    // Parked disabled player images can retain the historical off-map sentinel.
    if (!player) pImpl->npcAdmission.Reserve(
      formId, FormDesc::FromFormId(formId, espmFiles).ToString(),
      FormDesc::FromFormId(actor->GetBaseId(), espmFiles).ToString(),
      SrpNpcRegion(location, pos), player,
      SrpNpcSource(formId, actor->GetBaseId()));
  }
  struct AdmissionState
  {
    SrpNpcAdmission* gate;
    uint32_t id;
    bool* initializationFailed;
    bool started = false;
    bool inserted = false;
    bool complete = false;
  };
  AdmissionState admission{ &pImpl->npcAdmission, formId,
                            &pImpl->npcInitializationFailed };
  Viet::ScopedTask<AdmissionState> releaseOnFailure(
    [](AdmissionState& state) {
      if (!state.complete && state.started && state.gate->Enabled())
        *state.initializationFailed = true;
      if (!state.complete && !state.inserted)
        state.gate->Release(state.id);
    },
    admission);

  // Assign formIndex before Init
  admission.started = true;
  if (auto refr = form->AsObjectReference()) {
    if (!formIdxManager) {
      formIdxManager.reset(new MakeID(FormIndex::g_invalidIdx - 1));
    }

    if (!formIdxManager->CreateID(refr->idx)) {
      throw std::runtime_error("CreateID failed");
    }

    if (refrByIdxUnreliable.size() <= refr->GetIdx()) {
      refrByIdxUnreliable.resize(refr->GetIdx() + 1, nullptr);
    }
    refrByIdxUnreliable[refr->GetIdx()] = refr;
  }

  // MpObjectReference::Init requests save for newly created forms. That's why
  // we want formIndex to be assigned before init.
  form->Init(this, formId, optionalChangeFormToApply != nullptr);

  // MpForm's legacy destructor is not virtual. Auto-unloaded concrete actors
  // must retain a typed owner so their inventory/subscription/Impl members are
  // actually destroyed. Keep unknown subclasses on the existing resident path.
  std::shared_ptr<MpForm> ownedForm;
  if (pImpl->liveUnloadEnabled && pImpl->npcAdmission.HasLease(formId) &&
      typeid(*form) == typeid(MpActor))
    ownedForm = std::shared_ptr<MpActor>(static_cast<MpActor*>(form.release()));
  else
    ownedForm = std::move(form);
  auto it = forms.insert({ formId, std::move(ownedForm) }).first;
  // After insertion the actor is still loaded even if applying a corrupt saved
  // image throws. Keep its slot until destruction; never undercount live
  // forms.
  admission.inserted = true;

  if (optionalChangeFormToApply) {
    auto refr = it->second->AsObjectReference();
    if (!refr) {
      forms.erase(it); // Rollback changes due to exception
      admission.inserted = false;
      throw std::runtime_error(
        "Unable to apply ChangeForm, cast to ObjectReference failed");
    }
    refr->ApplyChangeForm(*optionalChangeFormToApply);
  }
  admission.complete = true;
}

std::string WorldState::SrpNpcRegion(const FormDesc& location,
                                     const NiPoint3& pos) const
{
  for (int i = 0; i < 3; ++i)
    if (!std::isfinite(pos[i]) || std::abs(pos[i]) > 100000000)
      throw std::runtime_error("SRP_NPC_ADMISSION_POSITION_INVALID");
  const auto name = location.ToString();
  if (espm) {
    const auto record =
      espm->GetBrowser().LookupById(location.ToFormId(espmFiles));
    if (!record.rec ||
        (record.rec->GetType() != "WRLD" && record.rec->GetType() != "CELL"))
      throw std::runtime_error("SRP_NPC_ADMISSION_LOCATION_INVALID");
    if (record.rec->GetType() == "CELL")
      return name;
  }
  return name + ":" +
    std::to_string(static_cast<int64_t>(std::floor(pos.x / 4096))) + ":" +
    std::to_string(static_cast<int64_t>(std::floor(pos.y / 4096)));
}
std::string WorldState::SrpNpcSource(uint32_t formId, uint32_t baseId) const
{
  if (!espm || !pImpl->npcAdmission.Enabled())
    return {};
  auto& browser = espm->GetBrowser();
  // An override retains the master's FormDesc. Use the winning record source,
  // and also gate dynamic/vanilla references using a controlled NPC_ base.
  std::string controlled;
  for (const auto id : { formId, baseId }) {
    const auto row = browser.LookupById(id);
    if (row.rec && row.fileIdx < espmFiles.size() &&
        pImpl->npcAdmission.ControlsPlugin(espmFiles[row.fileIdx])) {
      if (!controlled.empty() && controlled != espmFiles[row.fileIdx])
        throw std::runtime_error(
          "SRP_NPC_ADMISSION_AMBIGUOUS_SOURCE_REQUIRES_REVIEW");
      controlled = espmFiles[row.fileIdx];
    }
  }
  return controlled;
}
void WorldState::ConfigureSrpNpcAdmission(const nlohmann::json& config)
{
  if (!forms.empty())
    throw std::runtime_error("SRP_NPC_ADMISSION_CONFIG_BEFORE_LOAD_REQUIRED");
  SrpNpcAdmission validated;
  validated.Configure(config); // Reject malformed descriptors before
                               // FormDesc's legacy permissive parser.
  // Canonical descriptors must resolve in the ACTUAL load order, not an index
  // guessed from the catalog. Validate before replacing the admission policy.
  if (config.is_object())
    for (const auto& source :
         config.value("sources", nlohmann::json::array())) {
      const auto plugin = source.at("plugin").get<std::string>();
      if (!HasEspmFile(plugin))
        throw std::runtime_error("SRP_NPC_ADMISSION_PLUGIN_NOT_LOADED");
      for (const auto& desc : source.at("references")) {
        auto d = FormDesc::FromString(desc.get<std::string>());
        auto id = d.ToFormId(espmFiles);
        auto record = GetEspm().GetBrowser().LookupById(id);
        if (id >= 0xff000000 || !record.rec || record.rec->GetType() != "ACHR")
          throw std::runtime_error("SRP_NPC_ADMISSION_ACHR_REQUIRED");
        const auto baseId =
          record.ToGlobalId(reinterpret_cast<const espm::ACHR*>(record.rec)
                              ->GetData(GetEspmCache())
                              .baseId);
        const auto base = GetEspm().GetBrowser().LookupById(baseId);
        if (espmFiles.at(record.fileIdx) != plugin &&
            (!base.rec || espmFiles.at(base.fileIdx) != plugin))
          throw std::runtime_error(
            "SRP_NPC_ADMISSION_REFERENCE_SOURCE_MISMATCH");
      }
    }
  if (config.is_object())
    for (const auto& desc :
         config.value("faunaBases", nlohmann::json::array())) {
      auto d = FormDesc::FromString(desc.get<std::string>());
      auto record = GetEspm().GetBrowser().LookupById(d.ToFormId(espmFiles));
      if (!record.rec || record.rec->GetType() != "NPC_")
        throw std::runtime_error("SRP_NPC_ADMISSION_BASE_REQUIRED");
    }
  pImpl->npcAdmission.Configure(config);
}
nlohmann::json WorldState::InspectSrpNpcAdmission() const
{
  auto result = pImpl->npcAdmission.Inspect();
  result["initializationFailed"] = pImpl->npcInitializationFailed;
  result["deferredDynamicNpcs"] = pImpl->coldNpcIds.size();
  result["retiringNpcs"] = pImpl->retiringNpcIds.size();
  result["reservedNpcTombstones"] = pImpl->npcTombstoneIds.size();
  result["coldDynamicStorageVersion"] = 1;
  result["durableNpcRetirementVersion"] = 1;
  result["liveNpcUnloadVersion"] = 1;
  result["liveNpcUnloadEnabled"] = pImpl->liveUnloadEnabled;
  result["suspendingNpcs"] = pImpl->suspendingNpcs.size();
  result["liveUnloads"] = pImpl->liveUnloads;
  result["cancelledUnloads"] = pImpl->cancelledUnloads;
  result["maxNpcsPerHost"] = pImpl->maxNpcsPerHost;
  result["npcIndexQuarantineVersion"] = 1;
  result["quarantinedNetworkIndices"] = pImpl->retiredNetworkIndices.size();
  result["networkSessions"] = pImpl->networkSessions.size();
  return result;
}
bool WorldState::IsSrpOriginalControlled(uint32_t formId)
{
  if (formId >= 0xff000000 || !pImpl->npcAdmission.Enabled())
    return false;
  uint32_t baseId = 0;
  if (espm) {
    const auto row = espm->GetBrowser().LookupById(formId);
    if (row.rec && row.rec->GetType() == "ACHR")
      baseId = row.ToGlobalId(reinterpret_cast<const espm::ACHR*>(row.rec)
                                ->GetData(GetEspmCache())
                                .baseId);
  }
  return pImpl->npcAdmission.Controlled(
           FormDesc::FromFormId(formId, espmFiles).ToString()) ||
    !SrpNpcSource(formId, baseId).empty();
}
void WorldState::CheckSrpNpcMovement(uint32_t formId, const FormDesc& location,
                                     const NiPoint3& pos)
{
  // BeforeDestroy moves a deleted NPC out of the grid. It remains charged to
  // its old region until the final save and actual removal, not a new region.
  if (pImpl->preparingNpcIds.count(formId))
    return;
  if (pImpl->retiringNpcIds.count(formId))
    throw std::runtime_error("SRP_NPC_RETIREMENT_IN_PROGRESS");
  if (pImpl->npcAdmission.HasLease(formId) &&
      pImpl->npcAdmission.Move(formId, SrpNpcRegion(location, pos)) &&
      pImpl->npcAdmissionDeferred) {
    for (auto& [_, grid] : grids)
      grid.loadedChunks.clear();
    pImpl->npcAdmissionDeferred = false;
  }
}
void WorldState::ReleaseSrpNpcAdmission(uint32_t formId)
{
  const bool counted = pImpl->npcAdmission.HasLease(formId);
  pImpl->npcAdmission.Release(formId);
  pImpl->retiringNpcIds.erase(formId);
  pImpl->suspendingNpcs.erase(formId);
  hosters.erase(formId);
  if (counted && pImpl->npcAdmissionDeferred) {
    for (auto& [_, grid] : grids)
      grid.loadedChunks.clear();
    pImpl->npcAdmissionDeferred = false;
  }
}
void WorldState::RetainSrpNpcTombstone(const MpChangeForm& image)
{
  const auto& fields = image.dynamicFields.GetAsJson();
  const bool character = fields.contains("srpActiveCharacterId") &&
    !fields.at("srpActiveCharacterId").is_null() &&
    fields.at("srpActiveCharacterId") != "";
  if (!pImpl->npcAdmission.Enabled() || !image.isDeleted ||
      image.recType != MpChangeForm::ACHR || image.profileId >= 0 ||
      character || !image.formDesc.file.empty())
    return;
  const auto id = image.formDesc.ToFormId(espmFiles);
  if (image.baseDesc.ToFormId(espmFiles) <= 7)
    return;
  // A tombstone reserves identity without materializing an actor or its items.
  pImpl->npcTombstoneIds.insert(id);
  pImpl->changeFormsForDeferredLoad.erase(id);
  pImpl->allFormsByModIndexCache[255].reset();
}

bool WorldState::FinalizeSrpNpcRetirement(uint32_t formId)
{
  if (pImpl->liveUnloadEnabled && pImpl->npcAdmission.HasLease(formId) &&
      !pImpl->networkSessions.empty() && pImpl->retiredNetworkIndices.size() >= 65536)
    throw std::runtime_error("SRP_NPC_NETWORK_INDEX_QUARANTINE_SATURATED");
  if (!pImpl->retiringNpcIds.count(formId))
    return false;
  if (pImpl->preparingNpcIds.count(formId))
    throw std::runtime_error("SRP_NPC_RETIREMENT_TEARDOWN_IN_PROGRESS");
  auto& actor = GetFormAt<MpActor>(formId);
  const auto generation = GetChangeFormGeneration(formId);
  if (!actor.IsDeleted() || !actor.IsDisabled() || !generation ||
      !IsChangeFormSaved(formId, generation))
    throw std::runtime_error("SRP_NPC_RETIREMENT_REQUIRES_DURABLE_STATE");
  // BeforeDestroy has completed. Preserve that final image and prevent index
  // reuse until all its writes are acknowledged. Never clear an unsaved write.
  const auto idx = actor.GetIdx();
  if (pImpl->changesByIdx.size() > idx && pImpl->changesByIdx[idx])
    throw std::runtime_error("SRP_NPC_RETIREMENT_HAS_PENDING_WRITE");
  RetainSrpNpcTombstone(actor.GetChangeForm());
  if (!pImpl->npcTombstoneIds.count(formId)) {
    pImpl->npcInitializationFailed = true;
    throw std::runtime_error("SRP_NPC_RETIREMENT_IDENTITY_CHANGED");
  }
  return true;
}

void WorldState::TickSrpNpcRetirements()
{
  if (!pImpl->npcAdmission.Enabled())
    return;
  std::vector<uint32_t> candidates;
  for (const auto& [id, form] : forms) {
    auto* actor = form ? form->AsActor() : nullptr;
    if (id < 0xff000000 || !actor || actor->GetBaseId() <= 7 ||
        actor->GetChangeForm().profileId >= 0 || !actor->IsDeleted() ||
        !actor->IsDisabled())
      continue;
    const auto& fields = actor->GetDynamicFields().GetAsJson();
    if (fields.contains("srpActiveCharacterId") &&
        !fields.at("srpActiveCharacterId").is_null() &&
        fields.at("srpActiveCharacterId") != "")
      continue;
    pImpl->suspendingNpcs.erase(id); // Deletion uses its own durable protocol.
    candidates.push_back(id);
  }
  for (auto id : candidates) {
    const auto it = forms.find(id);
    if (it == forms.end())
      continue;
    // Teardown callbacks may remove other candidates; keep this object alive
    // and never use a lookup that could materialize a replacement identity.
    auto keepAlive = it->second;
    auto& actor = *keepAlive->AsActor();
    const auto generation = GetChangeFormGeneration(id);
    if ((pImpl->liveUnloadEnabled && !pImpl->networkSessions.empty() &&
         pImpl->retiredNetworkIndices.size() >= 65536) ||
        !generation || !IsChangeFormSaved(id, generation))
      continue;
    if (pImpl->retiringNpcIds.insert(id).second) {
      pImpl->preparingNpcIds.insert(id);
      try {
        keepAlive->BeforeDestroy();
        pImpl->preparingNpcIds.erase(id);
      } catch (...) {
        // Trigger exits or teardown callbacks may have partially run. Restart
        // re-reads the last durable tombstone; do not save a partial teardown.
        pImpl->npcInitializationFailed = true;
        throw;
      }
    }
    const auto finalGeneration = GetChangeFormGeneration(id);
    if (IsChangeFormSaved(id, finalGeneration))
      DestroyForm<MpActor>(id);
  }
}

// HostStop has no client ACK/lease epoch in the current protocol. Never hand
// an old index to another reference while a pre-existing connection can still
// send an idx-only movement packet. A fresh connection cannot know older idxs.
void WorldState::NotifySrpNpcConnection(uint32_t userId, bool connected)
{
  if (connected) {
    if (pImpl->connectionSerial == std::numeric_limits<uint64_t>::max())
      throw std::runtime_error("SRP_NPC_CONNECTION_SERIAL_EXHAUSTED");
    pImpl->networkSessions[userId] = ++pImpl->connectionSerial;
    return;
  }
  pImpl->networkSessions.erase(userId);
  auto oldest = std::numeric_limits<uint64_t>::max();
  for (const auto& [_, serial] : pImpl->networkSessions)
    oldest = std::min(oldest, serial);
  for (auto it = pImpl->retiredNetworkIndices.begin();
       it != pImpl->retiredNetworkIndices.end();) {
    if (it->second < oldest) {
      if (!formIdxManager || !formIdxManager->DestroyID(it->first)) {
        pImpl->npcInitializationFailed = true;
        throw std::runtime_error("SRP_NPC_QUARANTINE_RELEASE_FAILED");
      }
      it = pImpl->retiredNetworkIndices.erase(it);
    } else {
      ++it;
    }
  }
}

bool WorldState::ReleaseSrpNpcIndex(uint32_t idx)
{
  const auto* ref = idx < refrByIdxUnreliable.size() ? refrByIdxUnreliable[idx] : nullptr;
  const bool guardedNpc = pImpl->liveUnloadEnabled && ref &&
    pImpl->npcAdmission.HasLease(ref->GetFormId());
  if (guardedNpc && !pImpl->networkSessions.empty() &&
      pImpl->retiredNetworkIndices.size() >= 65536)
    return false;
  if (idx < refrByIdxUnreliable.size())
    refrByIdxUnreliable[idx] = nullptr;
  if (idx < lastMovUpdateByIdx.size())
    lastMovUpdateByIdx[idx].reset();
  if (guardedNpc && !pImpl->networkSessions.empty()) {
    // Index budget differs from the active NPC budget. Bound memory without
    // assigning an ambiguous index; saturation retains the actor/slot.
    if (pImpl->retiredNetworkIndices.size() >= 65536)
      return false;
    return pImpl->retiredNetworkIndices.emplace(idx, pImpl->connectionSerial).second;
  }
  return formIdxManager->DestroyID(idx);
}

void WorldState::ConfigureSrpNpcLiveUnload(const nlohmann::json& config)
{
  if (!forms.empty())
    throw std::runtime_error("SRP_NPC_UNLOAD_CONFIG_BEFORE_LOAD_REQUIRED");
  if (!config.is_object() || config.size() != 4 ||
      config.value("schemaVersion", 0) != 1 ||
      !config.contains("enabled") || !config.at("enabled").is_boolean() ||
      !config.contains("graceMs") || !config.at("graceMs").is_number_unsigned() ||
      !config.contains("maxNpcsPerHost") ||
      !config.at("maxNpcsPerHost").is_number_unsigned())
    throw std::runtime_error("SRP_NPC_UNLOAD_CONFIG_INVALID");
  const auto grace = config.at("graceMs").get<uint64_t>();
  const auto cap = config.at("maxNpcsPerHost").get<uint64_t>();
  if (grace > 300000 || cap < 1 || cap > 180 ||
      (config.at("enabled").get<bool>() && !pImpl->npcAdmission.Enabled()))
    throw std::runtime_error("SRP_NPC_UNLOAD_CONFIG_INVALID");
  pImpl->liveUnloadEnabled = config.at("enabled").get<bool>();
  pImpl->liveUnloadGraceMs = uint32_t(grace);
  pImpl->maxNpcsPerHost = uint32_t(cap);
}

bool WorldState::IsSrpNpcSuspending(uint32_t id) const
{
  auto it = pImpl->suspendingNpcs.find(id);
  return it != pImpl->suspendingNpcs.end() && it->second.generation != 0;
}

bool WorldState::SrpNpcHasInterest(const MpActor& actor) const
{
  const auto region = ColdGridPos(actor.GetPos());
  for (const auto& [_, form] : forms) {
    const auto* player = form ? form->AsActor() : nullptr;
    if (!player || player == &actor ||
        player->GetUserId() == Networking::InvalidUserId ||
        player->GetCellOrWorld() != actor.GetCellOrWorld())
      continue;
    const auto p = ColdGridPos(player->GetPos());
    // Same regional interest envelope as GetNeighborsByPosition, plus actual
    // subscriptions (e.g. an observer attached before a position update).
    if ((std::abs(int(p.first) - int(region.first)) <= 1 &&
         std::abs(int(p.second) - int(region.second)) <= 1) ||
        actor.GetListeners().count(const_cast<MpActor*>(player)))
      return true;
  }
  return false;
}

bool WorldState::CanSrpNpcHost(const MpObjectReference& remote,
                                const MpActor& host) const
{
  if (!pImpl->liveUnloadEnabled || !remote.AsActor() ||
      !pImpl->npcAdmission.HasLease(remote.GetFormId()))
    return true; // Non-NPC systems and the legacy protocol are unchanged.
  if (host.GetUserId() == Networking::InvalidUserId || remote.IsDeleted() ||
      remote.IsDisabled() || IsSrpNpcSuspending(remote.GetFormId()) ||
      remote.GetCellOrWorld() != host.GetCellOrWorld() ||
      !remote.GetListeners().count(const_cast<MpActor*>(&host)))
    return false;
  const auto a = ColdGridPos(remote.GetPos()), b = ColdGridPos(host.GetPos());
  if (std::abs(int(a.first) - int(b.first)) > 1 ||
      std::abs(int(a.second) - int(b.second)) > 1)
    return false;
  uint32_t count = 0;
  for (const auto& [npc, owner] : hosters)
    if (owner == host.GetFormId() && npc != remote.GetFormId() &&
        pImpl->npcAdmission.HasLease(npc))
      ++count;
  return count < pImpl->maxNpcsPerHost;
}

void WorldState::ReleaseSrpNpcHost(uint32_t id)
{
  const auto h = hosters.find(id);
  if (h == hosters.end())
    return;
  const auto owner = forms.find(h->second), remote = forms.find(id);
  hosters.erase(h); // Revoke before callbacks or stale movement can arrive.
  if (remote == forms.end())
    return;
  if (owner != forms.end()) {
    auto* actor = owner->second->AsActor();
    if (actor && actor->GetUserId() != Networking::InvalidUserId) {
      HostStopMessage msg;
      msg.target = id < 0xff000000 ? uint64_t(id) + 0x100000000 : id;
      actor->SendToUser(msg, true);
    }
  }
  auto* ref = remote->second->AsObjectReference();
  if (ref) {
    ref->UpdateHoster(0);
    if (ref->GetIdx() < lastMovUpdateByIdx.size())
      lastMovUpdateByIdx[ref->GetIdx()].reset();
  }
}

bool WorldState::SrpNpcCanSuspend(const MpActor& actor)
{
  const auto id = actor.GetFormId();
  if (!espm || typeid(actor) != typeid(MpActor) ||
      !pImpl->npcAdmission.HasLease(id) ||
      actor.GetBaseId() <= 7 || actor.GetChangeForm().profileId >= 0 ||
      actor.GetUserId() != Networking::InvalidUserId || actor.IsDead() ||
      actor.IsDeleted() || actor.IsDisabled() ||
      actor.GetChangeForm().nextRelootDatetime != 0 ||
      actor.HasSrpPendingNpcOperations() ||
      pImpl->retiredNetworkIndices.size() >= 65536)
    return false;
  const auto& f = actor.GetDynamicFields().GetAsJson();
  if (f.contains("srpActiveCharacterId") &&
      !f.at("srpActiveCharacterId").is_null() && f.at("srpActiveCharacterId") != "")
    return false;
  if (id >= 0xff000000) {
    if (!f.contains("srpSpawnSource") ||
        (f.at("srpSpawnSource") != "FAUNA" &&
         f.at("srpSpawnSource") != "HUNTING_NATIVE"))
      return false;
  } else {
    // Only explicitly approved source references with a real durable identity
    // enter original unload. Vanilla quest actors never become eligible just
    // because they lack scripts. No source is enabled in the PUBLIC subset.
    if (!IsSrpOriginalControlled(id) ||
        !f.contains("private.srpOriginalReferenceV1"))
      return false;
    try {
      if (SrpOriginalLifecycle::Inspect(actor).at("state") != "ALIVE_OBSERVED")
        return false;
    } catch (...) { return false; }
    const auto original = espm->GetBrowser().LookupById(id);
    if (!original.rec || original.rec->GetType() != "ACHR" ||
        (original.rec->GetFlags() & (0x20 | 0x200 | 0x400 | 0x800)))
      return false; // Deleted, quest item, persistent or initially disabled.
    espm::ScriptData referenceScripts;
    original.rec->GetScriptData(&referenceScripts, GetEspmCache());
    if (!referenceScripts.scripts.empty())
      return false;
    if (!pImpl->questPinsScanned) {
      // Inspect immutable QUST reference/base bindings, including overrides,
      // without instantiating actors or executing a quest/Papyrus callback.
      for (const auto& quest : espm->GetBrowser().GetDistinctRecordsByType("QUST"))
        espm::RecordHeaderAccess::IterateFields(quest.rec,
          [&](const char* type, uint32_t size, const char* bytes) {
            if (std::memcmp(type, "ALFR", 4) == 0 ||
                std::memcmp(type, "ALUA", 4) == 0 ||
                std::memcmp(type, "ALCO", 4) == 0) {
              if (size != 4)
                throw std::runtime_error("SRP_NPC_QUEST_PIN_RECORD_INVALID");
              const auto raw = espm::ReadFieldScalar<uint32_t>(bytes, size);
              if (raw) pImpl->questPinnedForms.insert(quest.ToGlobalId(raw));
            }
          }, GetEspmCache());
      pImpl->questPinsScanned = true;
    }
    if (pImpl->questPinnedForms.count(id) ||
        pImpl->questPinnedForms.count(actor.GetBaseId()))
      return false;
  }
  if (activationChildsByActivationParent.count(id))
    return false;
  for (const auto& [_, children] : activationChildsByActivationParent)
    if (children.count(id))
      return false;
  if (!espm)
    return false;
  const auto base = espm->GetBrowser().LookupById(actor.GetBaseId());
  if (!base.rec || base.rec->GetType() != "NPC_")
    return false;
  // Reuse the engine-side template contract and the actor's already-selected,
  // durable chain. Never reroll a leveled template while deciding residency.
  const auto unprotected = EvaluateTemplateNoThrow<espm::NPC_::UseBaseData>(
    this, actor.GetBaseId(), actor.GetTemplateChain(),
    [](const auto&, const auto& data) {
      return !data.isEssential && !data.isProtected && !data.isUnique;
    }, nullptr);
  const auto scriptFree = EvaluateTemplateNoThrow<espm::NPC_::UseScript>(
    this, actor.GetBaseId(), actor.GetTemplateChain(),
    [this](const auto& record, const auto&) {
      espm::ScriptData scripts;
      record.rec->GetScriptData(&scripts, GetEspmCache());
      return scripts.scripts.empty();
    }, nullptr);
  // Unknown or incomplete inheritance stays resident, never presumed safe.
  return unprotected.value_or(false) && scriptFree.value_or(false);
}

void WorldState::TickSrpNpcLiveUnload()
{
  if (!pImpl->liveUnloadEnabled || pImpl->npcInitializationFailed)
    return;
  // Expire host leases independently from unloading eligibility. A disconnected
  // or remote host must not keep a corpse/quest actor's network lease forever.
  std::vector<uint32_t> revoke;
  for (const auto& [id, owner] : hosters) {
    const auto n = forms.find(id), h = forms.find(owner);
    if (n == forms.end() || h == forms.end() || !h->second->AsActor() ||
        !n->second->AsObjectReference() ||
        !CanSrpNpcHost(*n->second->AsObjectReference(), *h->second->AsActor()))
      revoke.push_back(id);
  }
  for (auto id : revoke)
    ReleaseSrpNpcHost(id);
  const auto now = std::chrono::steady_clock::now();
  std::vector<uint32_t> candidates;
  for (const auto& [id, form] : forms)
    if (form && form->AsActor()) candidates.push_back(id);
  for (auto id : candidates) {
    const auto found = forms.find(id);
    if (found == forms.end() || !found->second->AsActor())
      continue;
    auto keepAlive = found->second;
    auto& actor = *keepAlive->AsActor();
    if (!SrpNpcCanSuspend(actor) || SrpNpcHasInterest(actor)) {
      if (pImpl->suspendingNpcs.erase(id))
        ++pImpl->cancelledUnloads;
      continue;
    }
    const auto inserted = pImpl->suspendingNpcs.emplace(id, Impl::Suspension{now, 0});
    auto& pending = inserted.first->second;
    if (now - pending.since < std::chrono::milliseconds(pImpl->liveUnloadGraceMs))
      continue;
    if (!pending.generation) {
      ReleaseSrpNpcHost(id);
      RequestSave(actor); // Full, unchanged ChangeForm. No BeforeDestroy.
      pending.generation = GetChangeFormGeneration(id);
      continue;
    }
    const auto generation = GetChangeFormGeneration(id);
    const auto idx = actor.GetIdx();
    if (!generation || !IsChangeFormSaved(id, generation) ||
        (idx < pImpl->changesByIdx.size() && pImpl->changesByIdx[idx]))
      continue; // Includes later writes and failed/retried batches.
    const auto image = actor.GetChangeForm();
    // No observer exists and no script/trigger callbacks are eligible. These
    // two operations only detach subscriptions, without death/delete events.
    actor.UnsubscribeFromAll();
    actor.RemoveFromGridAndUnsubscribeAll();
    if (forms.find(id) == forms.end() || forms.at(id) != keepAlive ||
        GetChangeFormGeneration(id) != generation ||
        MpChangeForm::ToJson(actor.GetChangeForm()) != MpChangeForm::ToJson(image)) {
      pImpl->npcInitializationFailed = true;
      throw std::runtime_error("SRP_NPC_UNLOAD_CALLBACK_MUTATION_RESTART_REQUIRED");
    }
    // Reuse the already-versioned cold reader. No new disk schema, marker,
    // tombstone, respawn, item compensation, or position change is introduced.
    pImpl->changeFormsForDeferredLoad[id] = image;
    if (id >= 0xff000000) pImpl->coldNpcIds.insert(id);
    const auto region = ColdGridPos(image.position);
    pImpl->coldNpcRegions[{ image.worldOrCellDesc.ToFormId(espmFiles),
                            region.first, region.second }].insert(id);
    if (!ReleaseSrpNpcIndex(idx)) {
      pImpl->npcInitializationFailed = true;
      throw std::runtime_error("SRP_NPC_UNLOAD_INDEX_RELEASE_FAILED");
    }
    if (idx < refrByIdxUnreliable.size())
      refrByIdxUnreliable[idx] = nullptr;
    forms.erase(id);
    pImpl->suspendingNpcs.erase(id);
    pImpl->allFormsByModIndexCache[id >> 24].reset();
    ReleaseSrpNpcAdmission(id);
    ++pImpl->liveUnloads;
  }
}

std::vector<uint32_t> WorldState::GetLoadedActors(uint32_t modIndex) const
{
  if (modIndex > 255)
    throw std::runtime_error("SRP_LOADED_ACTORS_MOD_INDEX_INVALID");
  std::vector<uint32_t> ids;
  for (const auto& [id, form] : forms)
    if ((id >> 24) == modIndex && form && form->AsActor())
      ids.push_back(id);
  std::sort(ids.begin(), ids.end());
  return ids;
}

void WorldState::Tick()
{
  // A failed Init/ApplyChangeForm may have queued a partial image or exposed
  // indices. Never save/activate that world or retry it as a successful load.
  // Clear deliberately cannot reset this latch: only a fresh world may read
  // the unchanged durable image again after the cause is addressed.
  if (pImpl->npcInitializationFailed)
    throw std::runtime_error("SRP_NPC_INITIALIZATION_FAILED_RESTART_REQUIRED");
  const auto now = std::chrono::system_clock::now();
  TickSaveStorage(now);
  TickTimers(now);
  TickSrpNpcRetirements();
  TickSrpNpcLiveUnload();
}

bool WorldState::IsFormLoadingInProgress() const
{
  return pImpl->formLoadingInProgress;
}

void WorldState::LoadChangeForm(const MpChangeForm& changeForm,
                                const FormCallbacks& callbacks)
{
  Viet::ScopedTask<bool> task([](bool& st) { st = false; },
                              pImpl->formLoadingInProgress);
  pImpl->formLoadingInProgress = true;

  std::unique_ptr<MpObjectReference> form;

  const auto baseId = changeForm.baseDesc.ToFormId(espmFiles);
  const auto formId = changeForm.formDesc.ToFormId(espmFiles);

  std::string baseType = "STAT";
  if (espm) {
    const auto rec = espm->GetBrowser().LookupById(baseId).rec;

    if (!rec) {
      spdlog::warn("Skipping ChangeForm {:x}: unable to find base record {:x}",
                   formId, baseId);
      return;
    }
    baseType = rec->GetType().ToString();
  }

  if (espm) {
    if (!changeForm.appearanceDump.empty()) {
      simdjson::dom::parser p;
      auto res = p.parse(changeForm.appearanceDump);
      if (res.error()) {
        spdlog::warn(
          "Skipping ChangeForm {:x}: unable to parse appearanceDump", formId);
        return;
      }

      Appearance appearance;
      try {
        appearance = Appearance::FromJson(res.value());
      } catch (std::exception& e) {
        spdlog::warn("Skipping ChangeForm {:x}: Appearance::FromJson failed "
                     "on appearanceDump: {}",
                     formId, e.what());
        return;
      }

      const auto rec = espm->GetBrowser().LookupById(appearance.raceId).rec;

      if (!rec || rec->GetType().ToString() != "RACE") {
        spdlog::warn("Skipping ChangeForm {:x}: bad raceId in appearanceDump",
                     formId);
        return;
      }
    }
  }

  if (formId < 0xff000000) {
    auto it = forms.find(formId);
    if (it != forms.end()) {
      auto refr = std::dynamic_pointer_cast<MpObjectReference>(it->second);
      if (refr) {
        refr->ApplyChangeForm(changeForm);
      }
    } else {
      pImpl->changeFormsForDeferredLoad[formId] = changeForm;
    }
    return;
  }

  const auto& fields = changeForm.dynamicFields.GetAsJson();
  const bool hasCharacter = fields.contains("srpActiveCharacterId") &&
    !fields.at("srpActiveCharacterId").is_null() &&
    fields.at("srpActiveCharacterId") != "";
  if (pImpl->npcAdmission.Enabled() &&
      changeForm.recType == MpChangeForm::ACHR && changeForm.profileId < 0 &&
      baseId > 7 && !hasCharacter && !pImpl->loadingColdNpcIds.count(formId) &&
      !forms.count(formId)) {
    // Validate persisted location before indexing. Do not alter inventory,
    // receipts, death state or coordinates, and do not request a save here.
    const bool legacyParked = changeForm.position.x == -1000000000.f &&
      changeForm.position.y == 0 && changeForm.position.z == 0;
    try {
      if (!legacyParked)
        SrpNpcRegion(changeForm.worldOrCellDesc, changeForm.position);
    } catch (...) {
      // Recovery drivers catch per-record exceptions. Latch the whole world so
      // a rejected durable NPC cannot become a successful, incomplete boot.
      pImpl->npcInitializationFailed = true;
      throw;
    }
    const auto world = changeForm.worldOrCellDesc.ToFormId(espmFiles);
    pImpl->changeFormsForDeferredLoad[formId] = changeForm;
    pImpl->coldNpcIds.insert(formId);
    // Historical off-map images stay quarantined, not moved or deleted.
    if (!legacyParked) {
      const auto pos = ColdGridPos(changeForm.position);
      pImpl->coldNpcRegions[{ world, pos.first, pos.second }].insert(formId);
    }
    pImpl->allFormsByModIndexCache[255].reset();
    return;
  }

  switch (changeForm.recType) {
    case MpChangeForm::ACHR:
      form.reset(new MpActor(LocationalData(), callbacks, baseId));
      break;
    case MpChangeForm::REFR:
      form.reset(new MpObjectReference(LocationalData(), callbacks, baseId,
                                       baseType.data()));
      break;
    default:
      throw std::runtime_error("Unknown ChangeForm type: " +
                               std::to_string(changeForm.recType));
  }

  auto formRawPtr = reinterpret_cast<MpObjectReference*>(form.get());
  AddForm(std::move(form), formId, false, &changeForm);
  auto idx = formRawPtr->GetIdx();

  // EnsureBaseContainerAdded forces saving here.
  // We do not want characters to save when they are load partially
  // This behaviour results in
  // https://github.com/skyrim-multiplayer/issue-tracker/issues/64

  // So we expect that RequestSave does nothing in this case:

  if (pImpl->changesByIdx.size() > idx &&
      pImpl->changesByIdx[idx] != std::nullopt) {
    assert(false);

    // For Release configuration we just manually remove formId from changes
    pImpl->changesByIdx[idx] = std::nullopt;
  }
}

void WorldState::RequestReloot(MpObjectReference& ref,
                               std::chrono::system_clock::duration time)
{
  auto refPtr = &ref;

  std::optional<Viet::Promise<Viet::Void>> timer;

  constexpr auto kEpsilon = std::chrono::milliseconds(1);

  for (auto& entry : pImpl->relootTimeForTypes) {
    auto diff = entry.time - time;
    if (diff < kEpsilon && diff > -kEpsilon) {
      timer = entry.timer.SetTimer(time, nullptr);
      break;
    }
  }

  if (timer == std::nullopt) {
    timer = SetTimer(time);
  }

  timer->Then([this, refPtr, formId = ref.GetFormId()](Viet::Void) {
    auto& form = LookupFormById(formId);
    auto reference = form->AsObjectReference();
    // Check if the reference is still the same, not re-created with the same
    // id for example
    if (reference && reference == refPtr) {
      reference->DoReloot();
    }
  });
}

void WorldState::RequestSave(MpObjectReference& ref)
{
  if (pImpl->formLoadingInProgress) {
    return;
  }

  auto idx = ref.GetIdx();

  [[unlikely]] if (idx == FormIndex::g_invalidIdx) {
    return spdlog::error("RequestSave {:x} - Invalid index", ref.GetFormId());
  }

  if (pImpl->changesByIdx.size() <= idx) {
    pImpl->changesByIdx.resize(idx + 1);
  }

  pImpl->changesByIdx[idx] = ref.GetChangeForm();
  pImpl->changesByIdxEmpty = false;
  pImpl->saveGenerations.OnRequestSave(idx);
}

void WorldState::RetainOriginalForUnload(MpObjectReference& actor)
{
  const auto& fields=actor.GetDynamicFields().GetAsJson();
  if(actor.GetFormId()>=0xff000000 ||
    (!fields.contains("private.srpOriginalReferenceV1")&&
     !fields.contains("private.srpOriginalLifecycleV1")))return;
  const auto generation=GetChangeFormGeneration(actor.GetFormId());
  if(!generation || !IsChangeFormSaved(actor.GetFormId(),generation))
    throw std::runtime_error("SRP_ORIGINAL_UNLOAD_REQUIRES_DURABLE_STATE");
  // The lazy-load cache is a current image, never the old pre-death snapshot.
  pImpl->changeFormsForDeferredLoad[actor.GetFormId()]=actor.GetChangeForm();
  hosters.erase(actor.GetFormId()); // a network lease cannot survive unload
}

uint64_t WorldState::GetChangeFormGeneration(uint32_t formId) const
{
  if(pImpl->npcInitializationFailed)return 0;
  auto it = forms.find(formId);
  if (it == forms.end() || !it->second) {
    return 0;
  }
  auto refr = it->second->AsObjectReference();
  return refr ? pImpl->saveGenerations.GetRequested(refr->GetIdx()) : 0;
}

std::vector<nlohmann::json> WorldState::GetSrpOriginalEncounterImages()
{
  if(pImpl->npcInitializationFailed)throw std::runtime_error("SRP_NPC_INITIALIZATION_FAILED_RESTART_REQUIRED");
  std::vector<nlohmann::json> result;
  auto add=[&](uint32_t id,const DynamicFields& fields,const FormDesc& base,int32_t profile,bool durable){
    const auto& f=fields.GetAsJson();if(!f.contains(SrpOriginalEncounter::kProperty))return;
    if(id>=0xff000000 || profile>=0 || !f.contains("private.srpOriginalReferenceV1"))
      throw std::runtime_error("SRP_ENCOUNTER_ORIGINAL_IMAGE_CORRUPT");
    if(f.contains("srpActiveCharacterId") && !f.at("srpActiveCharacterId").is_null() && f.at("srpActiveCharacterId")!="")
      throw std::runtime_error("SRP_ENCOUNTER_ORIGINAL_IMAGE_CORRUPT");
    const auto& identity=f.at("private.srpOriginalReferenceV1");
    if(identity.at("reference")!=FormDesc::FromFormId(id,espmFiles).ToString() || identity.at("base")!=base.ToString())
      throw std::runtime_error("SRP_ENCOUNTER_ORIGINAL_IMAGE_CORRUPT");
    result.push_back({{"reference",identity.at("reference")},{"identity",identity},
      {"receipt",f.at(SrpOriginalEncounter::kProperty)},{"durable",durable}});
  };
  // These are database-loaded or explicitly save-confirmed unload images.
  for(const auto& [id,image]:pImpl->changeFormsForDeferredLoad){
    if(image.recType!=MpChangeForm::ACHR && image.dynamicFields.GetAsJson().contains(SrpOriginalEncounter::kProperty))
      throw std::runtime_error("SRP_ENCOUNTER_ORIGINAL_IMAGE_CORRUPT");
    add(id,image.dynamicFields,image.baseDesc,image.profileId,true);
  }
  for(const auto& [id,form]:forms){
    const auto* actor=form?form->AsActor():nullptr;if(!actor)continue;
    const auto& f=actor->GetDynamicFields().GetAsJson();
    if(!f.contains(SrpOriginalEncounter::kProperty))continue;
    const auto& receipt=f.at(SrpOriginalEncounter::kProperty);
    const auto g=GetChangeFormGeneration(id);
    auto [it,inserted]=pImpl->encounterSaveGenerations.try_emplace(id,std::make_pair(receipt,g));
    if(it->second.first!=receipt || (!it->second.second && g))it->second={receipt,g};
    bool durable=it->second.second && IsChangeFormSaved(id,it->second.second);
    for(const auto& stored:result)if(stored.at("reference")==FormDesc::FromFormId(id,espmFiles).ToString() && stored.at("receipt")==receipt && stored.at("durable")==true){durable=true;break;}
    add(id,actor->GetDynamicFields(),FormDesc::FromFormId(actor->GetBaseId(),espmFiles),actor->GetProfileId(),durable);
  }
  return result;
}

bool WorldState::IsChangeFormSaved(uint32_t formId, uint64_t generation) const
{
  if (pImpl->npcInitializationFailed)
    return false;
  auto it = forms.find(formId);
  if (it == forms.end() || !it->second) {
    return false;
  }
  auto refr = it->second->AsObjectReference();
  return refr && pImpl->saveGenerations.IsSaved(refr->GetIdx(), generation);
}

const std::shared_ptr<MpForm>& WorldState::LookupFormById(
  uint32_t formId, std::stringstream* optionalOutTrace)
{
  if (pImpl->npcInitializationFailed)
    throw std::runtime_error("SRP_NPC_INITIALIZATION_FAILED_RESTART_REQUIRED");
  static const std::shared_ptr<MpForm> kNullForm;

  if (optionalOutTrace) {
    *optionalOutTrace << "searching for " << std::hex << formId << std::endl;
  }

  auto it = forms.find(formId);
  if (it == forms.end()) {
    if (formId >= 0xff000000 && pImpl->coldNpcIds.count(formId)) {
      if (pImpl->loadingColdNpcIds.count(formId))
        return kNullForm;
      auto copy = pImpl->changeFormsForDeferredLoad.at(formId);
      pImpl->loadingColdNpcIds.insert(formId);
      struct Loading
      {
        std::set<uint32_t>* ids;
        uint32_t id;
      };
      Loading state{ &pImpl->loadingColdNpcIds, formId };
      Viet::ScopedTask<Loading> done([](Loading& s) { s.ids->erase(s.id); },
                                     state);
      try {
        LoadChangeForm(copy, formCallbacksFactory());
      } catch (const std::runtime_error& e) {
        if (std::string(e.what()).find("SRP_NPC_ADMISSION_") != 0)
          throw;
        pImpl->npcAdmissionDeferred = true;
        if (optionalOutTrace)
          *optionalOutTrace << e.what() << std::endl;
        return kNullForm;
      }
      it = forms.find(formId);
      if (it != forms.end()) {
        pImpl->coldNpcIds.erase(formId);
        const auto pos = ColdGridPos(copy.position);
        const auto zone = std::make_tuple(
          copy.worldOrCellDesc.ToFormId(espmFiles), pos.first, pos.second);
        auto region = pImpl->coldNpcRegions.find(zone);
        if (region != pImpl->coldNpcRegions.end()) {
          region->second.erase(formId);
          if (region->second.empty())
            pImpl->coldNpcRegions.erase(region);
        }
        return it->second;
      }
      throw std::runtime_error("SRP_COLD_NPC_LOAD_DID_NOT_MATERIALIZE");
    }
    if (formId < 0xff000000) {
      if (LoadForm(formId, optionalOutTrace)) {
        it = forms.find(formId);
        if (it != forms.end()) {
          auto saved = pImpl->changeFormsForDeferredLoad.find(formId);
          if (saved != pImpl->changeFormsForDeferredLoad.end()) {
            const auto pos = ColdGridPos(saved->second.position);
            const auto zone = std::make_tuple(saved->second.worldOrCellDesc.ToFormId(espmFiles), pos.first, pos.second);
            auto cold = pImpl->coldNpcRegions.find(zone);
            if (cold != pImpl->coldNpcRegions.end()) {
              cold->second.erase(formId);
              if (cold->second.empty()) pImpl->coldNpcRegions.erase(cold);
            }
          }
          if (optionalOutTrace) {
            *optionalOutTrace << "found after successful LoadForm" << std::hex
                              << formId << std::endl;
          }
          return it->second;
        }
        if (optionalOutTrace) {
          *optionalOutTrace << "not found after successful LoadForm"
                            << std::hex << formId << std::endl;
        }
        return kNullForm;
      } else {
        if (optionalOutTrace) {
          *optionalOutTrace << "LoadForm returned false " << std::hex << formId
                            << std::endl;
        }
      }
    }
    if (optionalOutTrace) {
      *optionalOutTrace << "not found " << std::hex << formId << std::endl;
    }
    return kNullForm;
  }

  if (optionalOutTrace) {
    *optionalOutTrace << "found " << std::hex << formId << std::endl;
  }
  return it->second;
}

const std::shared_ptr<MpForm>& WorldState::LookupFormByIdNoLoad(
  uint32_t formId)
{
  if(pImpl->npcInitializationFailed)
    throw std::runtime_error("SRP_NPC_INITIALIZATION_FAILED_RESTART_REQUIRED");
  static const std::shared_ptr<MpForm> kNullForm;

  auto it = forms.find(formId);
  if (it == forms.end()) {
    return kNullForm;
  }

  return it->second;
}

bool WorldState::AttachEspmRecord(const espm::CombineBrowser& br,
                                  const espm::RecordHeader* record,
                                  const espm::IdMapping& mapping,
                                  std::stringstream* optionalOutTrace)
{
  auto& cache = GetEspmCache();
  // this place is a hotpath.
  // we want to use reinterpret_cast<> instead of espm::Convert<>
  // in order to reduce amount of generated assembly code
  auto* refr = reinterpret_cast<const espm::REFR*>(record);
  auto data = refr->GetData(cache);

  uint32_t baseId = espm::utils::GetMappedId(data.baseId, mapping);
  espm::LookupResult base = br.LookupById(baseId);
  if (!base.rec) {
    logger->info("baseId {} {}", baseId, static_cast<const void*>(base.rec));
    if (optionalOutTrace) {
      *optionalOutTrace << fmt::format(
        "AttachEspmRecord - base record not found {:x} \n", baseId);
    }
    return false;
  }

  espm::Type t = base.rec->GetType();
  bool isNpc = t == "NPC_";
  bool isFurniture = t == "FURN";
  bool isActivator = t == "ACTI";
  bool isDoor = t == "DOOR";
  bool isContainer = t == "CONT";
  bool isFlor = t == "FLOR" &&
    reinterpret_cast<const espm::FLOR*>(base.rec)->GetData(cache).resultItem;
  bool isTree = t == "TREE" &&
    reinterpret_cast<const espm::TREE*>(base.rec)->GetData(cache).resultItem;

  if (!isNpc && !isFurniture && !isActivator && !espm::utils::IsItem(t) &&
      !isDoor && !isContainer && !isFlor && !isTree) {
    if (optionalOutTrace) {
      *optionalOutTrace << fmt::format(
        "AttachEspmRecord - the server skips base type {} \n", t.ToString());
    }
    return false;
  }

  bool startsDead = false;
  if (isNpc) {
    auto* achr = reinterpret_cast<const espm::ACHR*>(record);
    startsDead = achr->StartsDead();
    if (startsDead) {
      if (optionalOutTrace) {
        *optionalOutTrace << fmt::format(
          "AttachEspmRecord - the server skips dead actors\n");
      }
      return false; // TODO: Load dead references
    }
  }

  // TODO: Load disabled references
  enum
  {
    InitiallyDisabled = 0x800,
    DeletedRecord = 0x20
  };

  if (refr->GetFlags() & InitiallyDisabled) {
    if (optionalOutTrace) {
      *optionalOutTrace << fmt::format(
        "AttachEspmRecord - the server skips initially disabled references\n");
    }
    return false;
  }

  if (refr->GetFlags() & DeletedRecord) {
    if (optionalOutTrace) {
      *optionalOutTrace << fmt::format(
        "AttachEspmRecord - the server skips deleted references\n");
    }
    return false;
  }

  if (!npcEnabled && isNpc) {
    if (optionalOutTrace) {
      *optionalOutTrace << fmt::format(
        "AttachEspmRecord - the server skips npcs: npcEnabled = false\n");
    }
    return false;
  }

  uint32_t formId = espm::utils::GetMappedId(record->GetId(), mapping);

  if (isNpc) {
    if (NpcSourceFilesOverriden() && !IsNpcAllowed(formId)) {
      spdlog::trace("Skip NPC loading, it is not allowed. refrId {:#x}",
                    formId);
      if (optionalOutTrace) {
        *optionalOutTrace
          << fmt::format("Skip NPC loading, it is not allowed. refrId {:#x}",
                         formId)
          << std::endl;
      }
      return false;
    }
    auto npcData =
      reinterpret_cast<const espm::NPC_*>(base.rec)->GetData(cache);

    if (npcData.isEssential || npcData.isProtected || npcData.isUnique) {
      if (optionalOutTrace) {
        *optionalOutTrace << fmt::format("Skip NPC due to its flags")
                          << std::endl;
      }
      return false;
    }

    enum class ListType : uint32_t
    {
      CrimeFactionsList = 0x26953
    };

    auto factionBaseId = static_cast<std::underlying_type_t<ListType>>(
      ListType::CrimeFactionsList);
    espm::LookupResult res = br.LookupById(factionBaseId);
    auto* formList = reinterpret_cast<const espm::FLST*>(res.rec);
    std::vector<uint32_t> factionFormIds = formList->GetData(cache).formIds;
    for (auto& formId : factionFormIds) {
      formId = res.ToGlobalId(formId);
    }

    for (auto fact : npcData.factions) {
      auto it = std::find(factionFormIds.begin(), factionFormIds.end(),
                          base.ToGlobalId(fact.formId));
      if (it != factionFormIds.end()) {
        logger->info("Skipping actor {:#x} because it's in faction {:#x}",
                     record->GetId(), *it);
        if (optionalOutTrace) {
          *optionalOutTrace << fmt::format("Skip NPC due to faction")
                            << std::endl;
        }
        return false;
      }
    }

    const int kPcLevel = 0; // Shouldn't mean much to races

    // May be not exact the same template chain as it would be in MpActor
    // but it's ok for this check
    std::vector<uint32_t> evaluateChainResult =
      LeveledListUtils::EvaluateTemplateChain(br, base, kPcLevel);
    std::vector<FormDesc> templateChain(evaluateChainResult.size());
    std::transform(evaluateChainResult.begin(), evaluateChainResult.end(),
                   templateChain.begin(), [&](uint32_t formId) {
                     return FormDesc::FromFormId(formId, this->espmFiles);
                   });

    uint32_t race = EvaluateTemplate<espm::NPC_::UseTraits>(
      this, baseId, templateChain,
      [&](const auto& npcLookupResult, const auto& npcData) {
        return npcLookupResult.ToGlobalId(npcData.race);
      });

    bool isBanned = std::find(bannedEspmCharacterRaceIds.begin(),
                              bannedEspmCharacterRaceIds.end(),
                              race) != bannedEspmCharacterRaceIds.end();
    if (isBanned) {
      logger->info("Skipping actor {:#x} because it has banned race {:#x}",
                   record->GetId(), race);
      if (optionalOutTrace) {
        *optionalOutTrace << fmt::format("Skip NPC due to banned race")
                          << std::endl;
      }
      return false;
    }
  }

  auto locationalData = data.loc;

  uint32_t worldOrCell =
    espm::utils::GetMappedId(GetWorldOrCell(br, record), mapping);
  if (!worldOrCell) {
    logger->error("Anomaly: refr without world/cell");
    if (optionalOutTrace) {
      *optionalOutTrace << fmt::format("Anomaly: refr without world/cell")
                        << std::endl;
    }
    return false;
  }

  if (isNpc && NpcSourceFilesOverriden()) {
    bool isInterior = false, isExterior = false,
         spawnInInterior = defaultSetting.spawnInInterior,
         spawnInExterior = defaultSetting.spawnInExterior;
    espm::LookupResult res = br.LookupById(worldOrCell);
    auto* cellRecord = espm::Convert<espm::CELL>(res.rec);
    if (cellRecord) {
      isInterior = true;
    }
    auto* worldRecord = espm::Convert<espm::WRLD>(res.rec);
    if (worldRecord) {
      isExterior = true;
    }
    uint32_t npcFileIdx = GetFileIdx(formId);
    if (npcFileIdx >= espmFiles.size()) {
      spdlog::error("NPC's idx is greater than espmFiles.size(). NPC's"
                    "formId "
                    "{:#x}, espmFiles size: {}",
                    formId, espmFiles.size());
      if (optionalOutTrace) {
        *optionalOutTrace
          << fmt::format("NPC's idx is greater than espmFiles.size(). NPC's"
                         "formId "
                         "{:#x}, espmFiles size: {}",
                         formId, espmFiles.size())
          << std::endl;
      }
      return false;
    }
    auto it = npcSettings.find(espmFiles[npcFileIdx]);
    if (it != npcSettings.end()) {
      spawnInInterior = it->second.spawnInInterior;
      spawnInExterior = it->second.spawnInExterior;
      spdlog::trace(
        "found npc setting spawnInInterior: {}, spawnInExterior: {}",
        spawnInInterior, spawnInExterior);
    } else {
      spdlog::trace("npc setting has not been found. Use default "
                    "spawnInInterior: {} and spawnInExterior: {}",
                    spawnInInterior, spawnInExterior);
    }

    if (spawnInInterior && isInterior || spawnInExterior && isExterior) {
    }

    if ((!spawnInInterior || !isInterior) &&
        (!spawnInExterior || !isExterior)) {
      spdlog::trace(
        "Unable to spawn npc because of "
        "rules applied in server settings: spawnInInterior={}, "
        "spawnInExterior={}, NPC location: exterior={}, interior={}",
        spawnInInterior, spawnInExterior, isExterior, isInterior);
      if (optionalOutTrace) {
        *optionalOutTrace
          << fmt::format(
               "Unable to spawn npc because of "
               "rules applied in server settings: spawnInInterior={}, "
               "spawnInExterior={}, NPC location: exterior={}, interior={}",
               spawnInInterior, spawnInExterior, isExterior, isInterior)
          << std::endl;
      }
      return false;
    }
  }

  if (!locationalData) {
    logger->error("Anomaly: refr without locationalData");
    if (optionalOutTrace) {
      *optionalOutTrace << fmt::format("Anomaly: refr without locationalData")
                        << std::endl;
    }
    return false;
  }

  std::optional<NiPoint3> primitiveBoundsDiv2;
  if (data.boundsDiv2) {
    primitiveBoundsDiv2 =
      NiPoint3(data.boundsDiv2[0], data.boundsDiv2[1], data.boundsDiv2[2]);
  }

  auto typeStr = t.ToString();
  std::unique_ptr<MpForm> form;
  LocationalData formLocationalData = {
    LocationalDataUtils::GetPos(locationalData),
    LocationalDataUtils::GetRot(locationalData),
    FormDesc::FromFormId(worldOrCell, espmFiles)
  };

  MpChangeFormREFR* changeForm = nullptr;
  if (!isNpc) {
    form.reset(new MpObjectReference(formLocationalData,
                                     formCallbacksFactory(), baseId,
                                     typeStr.data(), primitiveBoundsDiv2));
  } else {
    form.reset(
      new MpActor(formLocationalData, formCallbacksFactory(), baseId));
  }
  try{AddForm(std::move(form), formId, true, changeForm);}
  catch(const std::runtime_error& e){
    if(std::string(e.what()).find("SRP_NPC_ADMISSION_")!=0)throw;
    pImpl->npcAdmissionDeferred=true;
    if(optionalOutTrace)*optionalOutTrace<<e.what()<<std::endl;
    return false;
  }

  // Do not TriggerFormInitEvent here, doing it later after changeForm apply

  if (optionalOutTrace) {
    *optionalOutTrace << fmt::format("AttachEspmRecord returned true")
                      << std::endl;
  }
  return true;
}

bool WorldState::LoadForm(uint32_t formId, std::stringstream* optionalOutTrace)
{
  if(pImpl->npcInitializationFailed)
    throw std::runtime_error("SRP_NPC_INITIALIZATION_FAILED_RESTART_REQUIRED");
  // Chunk discovery calls LoadForm directly, even for already loaded actors.
  // Initializing another object for the same FormID queues an orphan snapshot
  // under another idx, which can overwrite the canonical actor in one batch.
  if (forms.find(formId) != forms.end())
    return true;
  ANTIGO_CONTEXT_INIT(ctx);
  ctx.AddUnsigned(formId);

  auto& br = GetEspm().GetBrowser();

  auto lookupRes = br.LookupById(formId);

  if (!lookupRes.rec) {
    return false;
  }

  auto mapping = br.GetCombMapping(lookupRes.fileIdx);

  bool attached =
    AttachEspmRecord(br, lookupRes.rec, *mapping, optionalOutTrace);
  if (optionalOutTrace) {
    *optionalOutTrace << "AttachEspmRecord " << (attached ? "true" : "false")
                      << std::endl;
  }

  if (!attached) {
    pImpl->attachEspmRecordFailures.push_back(formId);
  }

  if (attached) {
    auto& refr = GetFormAt<MpObjectReference>(formId);
    auto it = pImpl->changeFormsForDeferredLoad.find(formId);
    if (it != pImpl->changeFormsForDeferredLoad.end()) {
      auto copy = it->second; // crashes without copying
      try {
        refr.ApplyChangeForm(copy);
      } catch (...) {
        if (pImpl->npcAdmission.Enabled() && refr.AsActor())
          pImpl->npcInitializationFailed = true;
        throw;
      }
    }

    refr.ForceSubscriptionsUpdate();
  }

  return attached;
}

void WorldState::TickSaveStorage(const std::chrono::system_clock::time_point&)
{
  if (!pImpl->saveStorage) {
    return;
  }

  try {
    pImpl->saveStorage->Tick();
  } catch (
    Viet::AsyncSaveStorage<MpChangeForm, FormDesc,
                           std::vector<FormDesc>>::UpsertFailedException& e) {
    spdlog::error(
      "TickSaveStorage - received UpsertFailedException {}, re-saving",
      e.what());

    // No SetTimer here because timers may also break in theory. we faced such
    // problems earlier
    pImpl->saveStorageBusy = false;
    pImpl->saveGenerations.OnBatchFailed();

    auto& forms = e.GetAffectedForms();
    size_t numRequested = 0;

    for (size_t i = 0; i < forms.size(); ++i) {
      auto& changeForm = forms[i];
      if (changeForm == std::nullopt) {
        continue;
      }

      MpForm* form = LookupFormByIdx(static_cast<int>(i));
      MpObjectReference* refr = form ? form->AsObjectReference() : nullptr;
      if (!refr) {
        continue;
      }

      auto formId = changeForm->formDesc.ToFormId(espmFiles);

      if (refr->GetFormId() != formId) {
        spdlog::error("TickSaveStorage - formIds not matching {:x} <=> {:x}",
                      refr->GetFormId(), formId);
        continue;
      }

      RequestSave(*refr);
      numRequested++;
    }

    spdlog::info("TickSaveStorage - requested re-save for {} forms in buffer "
                 "with size {}",
                 numRequested, forms.size());

  } catch (std::exception& e) {
    spdlog::error(
      "TickSaveStorage - received std::exception {}, can't request re-save",
      e.what());
    pImpl->saveStorageBusy = false;
    pImpl->saveGenerations.OnBatchFailed();
  }

  // Upsert callbacks only ran inside saveStorage->Tick(); fire the gamemode
  // event here, outside of the storage's callback loop
  if (!pImpl->savedBatchesToNotify.empty()) {
    auto batches = std::move(pImpl->savedBatchesToNotify);
    pImpl->savedBatchesToNotify.clear();
    for (auto batchId : batches) {
      ChangeFormsSavedEvent event(batchId);
      event.Fire(this);
    }
  }

  if (pImpl->saveStorageBusy) {
    return;
  }

  if (pImpl->changesByIdxEmpty) {
    return;
  }

  pImpl->saveStorageBusy = true;

  auto pImpl_ = pImpl;

  auto previousSize = pImpl->changesByIdx.size();

  std::vector<uint32_t> batchIdxs;
  for (size_t i = 0; i < pImpl->changesByIdx.size(); ++i) {
    if (pImpl->changesByIdx[i] != std::nullopt) {
      batchIdxs.push_back(static_cast<uint32_t>(i));
    }
  }
  auto batchId = pImpl->saveGenerations.BeginBatch(batchIdxs);

  try {
    pImpl->saveStorage->Upsert(std::move(pImpl->changesByIdx),
                               [pImpl_, batchId] {
                                 pImpl_->saveStorageBusy = false;
                                 pImpl_->saveGenerations.OnBatchSaved(batchId);
                                 pImpl_->savedBatchesToNotify.push_back(
                                   batchId);
                               });
    pImpl->changesByIdxEmpty = true;
  } catch (std::exception& e) {
    pImpl->saveStorageBusy = false;
    pImpl->saveGenerations.OnBatchFailed();
    spdlog::error("TickSaveStorage - Upsert failed with {}", e.what());
  }

  pImpl->changesByIdx.clear();

  bool recycleSuccess =
    pImpl->saveStorage->GetRecycledChangeFormsBuffer(pImpl->changesByIdx);

  if (!recycleSuccess) {
    // Just resize to previous size, at least not to resize every RequestSave
    pImpl->changesByIdx.resize(previousSize);
  }
}

void WorldState::TickTimers(const std::chrono::system_clock::time_point&)
{
  timerEffects.TickTimers();
  timerRegular.TickTimers();

  for (auto& entry : pImpl->relootTimeForTypes) {
    entry.timer.TickTimers();
  }
}

void WorldState::SendPapyrusEvent(MpForm* form, const char* eventName,
                                  const VarValue* arguments,
                                  size_t argumentsCount)
{
  // MpForm already fired the gamemode PapyrusEventEvent, and the reference
  // initialized its script bindings before reaching here. A scriptless object
  // has no VM receivers: do not manufacture a raw VM handle just for OnLoad.
  // Real VM references remain a conservative pin in the unload eligibility.
  if (form->ListActivePexInstances().empty())
    return;
  std::vector<VarValue> args = { arguments, arguments + argumentsCount };

  if (spdlog::should_log(spdlog::level::trace)) {
    std::vector<std::string> argsStrings(args.size());
    for (size_t i = 0; i < args.size(); ++i) {
      argsStrings[i] = args[i].ToString();
    }

    if (!strcmp(eventName, "OnTrigger")) {
      static std::once_flag g_once;
      std::call_once(g_once, [&] {
        spdlog::trace("WorldState::SendPapyrusEvent {:x} - {} [{}]",
                      form->GetFormId(), eventName,
                      fmt::join(argsStrings, ", "));
        spdlog::trace("WorldState::SendPapyrusEvent {:x} - Muting {} globally "
                      "to keep logs clear",
                      form->GetFormId(), eventName);
      });
    } else {
      spdlog::trace("WorldState::SendPapyrusEvent {:x} - {} [{}]",
                    form->GetFormId(), eventName,
                    fmt::join(argsStrings, ", "));
    }
  }

  VirtualMachine::OnEnter onEnter = [&](const StackData& stackData) {
    pImpl->policy->BeforeSendPapyrusEvent(
      form, eventName, arguments, argumentsCount,
      stackData.stackIdHolder.GetStackId());
  };
  auto& vm = GetPapyrusVm();
  return vm.SendEvent(form->ToGameObject(), eventName, args, onEnter);
}

const std::set<MpObjectReference*>& WorldState::GetNeighborsByPosition(
  uint32_t cellOrWorld, int16_t cellX, int16_t cellY)
{
  if (espm && !pImpl->chunkLoadingInProgress) {
    Viet::ScopedTask<bool> task([](bool& st) { st = false; },
                                pImpl->chunkLoadingInProgress);
    pImpl->chunkLoadingInProgress = true;

    auto& br = espm->GetBrowser();
    // Dynamic legacy NPCs have no ESP placement. Index their saved coordinates
    // in the existing interest grid; never replace them with a new spawn.
    for (int x = int(cellX) - 1; x <= int(cellX) + 1; ++x)
      for (int y = int(cellY) - 1; y <= int(cellY) + 1; ++y) {
        if (x < INT16_MIN || x > INT16_MAX || y < INT16_MIN || y > INT16_MAX)
          continue;
        auto region =
          pImpl->coldNpcRegions.find({ cellOrWorld, int16_t(x), int16_t(y) });
        if (region == pImpl->coldNpcRegions.end())
          continue;
        const std::vector<uint32_t> ids(region->second.begin(),
                                        region->second.end());
        for (auto id : ids)
          LookupFormById(id);
      }
    for (int16_t x = cellX - 1; x <= cellX + 1; ++x) {
      for (int16_t y = cellY - 1; y <= cellY + 1; ++y) {
        const bool loaded = grids[cellOrWorld].loadedChunks[x][y];
        if (!loaded) {
          for (size_t i = 0; i < espmFiles.size(); ++i) {
            auto combMapping = br.GetCombMapping(i);
            auto rawMapping = br.GetRawMapping(i);
            uint32_t mappedCellOrWorld =
              espm::utils::GetMappedId(cellOrWorld, *rawMapping);
            auto records = br.GetRecordsAtPos(mappedCellOrWorld, x, y);
            for (auto rec : *records[i]) {
              const auto originalId = rec->GetId();
              const auto mappedId =
                espm::utils::GetMappedId(originalId, *combMapping);
              // 0xff in the mapping means no declared plugin owns this ID.
              // A plugin record must never enter the runtime/dynamic namespace.
              if (mappedId >= 0xff000000) {
                spdlog::error(
                  "WorldState::GetNeighborsByPosition - rejected plugin "
                  "reference: source={} recordType={} originalId=0x{:08x} "
                  "localPluginIndex={} mappedId=0x{:08x} "
                  "reason=unresolved-plugin-index cellOrWorld=0x{:08x} "
                  "chunkX={} chunkY={}",
                  espmFiles[i], rec->GetType().ToString(), originalId,
                  originalId >> 24, mappedId, cellOrWorld, x, y);
                continue;
              }
              assert(mappedId < 0xff000000);
              LoadForm(mappedId);
            }
          }
          // Do not keep "loaded" reference here since LoadForm would
          // invalidate this reference
          grids[cellOrWorld].loadedChunks[x][y] = true;
        }
      }
    }
  }

  auto& neighbours =
    grids[cellOrWorld].grid->GetNeighboursByPosition(cellX, cellY);
  return neighbours;
}

std::shared_ptr<std::vector<uint32_t>> WorldState::GetAllForms(
  uint32_t modIndex)
{
  if (modIndex >= std::size(pImpl->allFormsByModIndexCache)) {
    spdlog::error("WorldState::GetAllForms - Invalid mod index {}", modIndex);
    return nullptr;
  }

  auto& resCache = pImpl->allFormsByModIndexCache[modIndex];

  if (!resCache) {
    // Deduplicate form IDs
    // TODO: Consider cleaning changeFormsForDeferredLoad after adding a form
    // so we don't need de-duplicate in runtime
    std::unordered_set<uint32_t> formIds;
    for (const auto& p : forms) {
      if ((p.first >> 24) == modIndex) {
        formIds.insert(p.first);
      }
    }
    for (const auto& p : pImpl->changeFormsForDeferredLoad) {
      if ((p.first >> 24) == modIndex) {
        formIds.insert(p.first);
      }
    }

    for (auto id : pImpl->npcTombstoneIds) {
      if ((id >> 24) == modIndex)
        formIds.insert(id);
    }
    for (auto failedToAttachFormId : pImpl->attachEspmRecordFailures) {
      formIds.erase(failedToAttachFormId);
    }

    resCache =
      std::make_shared<std::vector<uint32_t>>(formIds.begin(), formIds.end());
  }

  return resCache;
}

MpForm* WorldState::LookupFormByIdx(int idx)
{
  if (formIdxManager) {
    if (idx >= 0 && idx < refrByIdxUnreliable.size()) {
      auto refr = refrByIdxUnreliable[idx];
      if (refr && refr->GetIdx() == idx) {
        return refr;
      }
    }
  }
  return nullptr;
}

espm::Loader& WorldState::GetEspm() const
{
  if (!espm) {
    throw std::runtime_error("No espm attached");
  }
  return *espm;
}

bool WorldState::HasEspm() const
{
  return !!espm;
}

espm::CompressedFieldsCache& WorldState::GetEspmCache()
{
  if (!espmCache) {
    throw std::runtime_error("No espm cache found");
  }
  return *espmCache;
}

IScriptStorage* WorldState::GetScriptStorage() const
{
  return pImpl->scriptStorage.get();
}

struct LazyState
{
  std::shared_ptr<PexScript> pex;
  std::vector<uint8_t> pexBin;

  // With Papyrus hotreload enabled, this variable hold references to
  // previous versions of pex files. This prevents the invalidation of
  // string/identifier types of VarValue
  std::vector<std::shared_ptr<PexScript>> oldPexHolder;
};

PexScript::Lazy CreatePexScriptLazy(
  const CIString& required, std::shared_ptr<IScriptStorage> scriptStorage,
  std::shared_ptr<spdlog::logger> logger, bool enableHotReload)
{
  auto lazyState = std::make_shared<LazyState>();

  PexScript::Lazy lazy;
  lazy.source = required.data();
  lazy.fn = [lazyState, scriptStorage, required, logger, enableHotReload]() {
    if (enableHotReload) {
      auto requiredPex = scriptStorage->GetScriptPex(required.data());
      if (requiredPex != lazyState->pexBin) {
        lazyState->oldPexHolder.push_back(lazyState->pex);
        lazyState->pex.reset();
        lazyState->pexBin = requiredPex;
        logger->info("Papyrus script {} has been reloaded", required);
      }
    }

    if (!lazyState->pex) {
      auto requiredPex = scriptStorage->GetScriptPex(required.data());
      if (requiredPex.empty()) {
        throw std::runtime_error(
          "'" + std::string({ required.begin(), required.end() }) +
          "' is listed but failed to "
          "load from the storage");
      }
      auto pexStructure = Reader({ requiredPex }).GetSourceStructures();
      lazyState->pex = pexStructure[0];
    }
    return lazyState->pex;
  };

  return lazy;
}

VirtualMachine& WorldState::GetPapyrusVm()
{
  if (!pImpl->vm) {
    std::vector<PexScript::Lazy> pexStructures;
    std::vector<std::string> scriptNames;

    auto scriptStorage = pImpl->scriptStorage;
    if (!scriptStorage) {
      logger->error("Required scriptStorage to be non-null");
      pImpl->vm.reset(
        new VirtualMachine(std::vector<std::shared_ptr<PexScript>>()));
      return *pImpl->vm;
    }

    auto& scripts = scriptStorage->ListScripts(false);
    for (auto& required : scripts) {
      auto lazy = CreatePexScriptLazy(required, scriptStorage, this->logger,
                                      this->isPapyrusHotReloadEnabled);
      pexStructures.push_back(lazy);
    }

    if (!pexStructures.empty()) {
      pImpl->vm.reset(new VirtualMachine(pexStructures));

      pImpl->vm->SetMissingScriptHandler(
        [scriptStorage, this](std::string className) {
          std::optional<PexScript::Lazy> result;

          CIString classNameCi = { className.begin(), className.end() };
          if (scriptStorage->ListScripts(true).count(classNameCi)) {
            result =
              CreatePexScriptLazy(classNameCi, scriptStorage, this->logger,
                                  this->isPapyrusHotReloadEnabled);
          }
          return result;
        });

      pImpl->classes =
        PapyrusClassesFactory::CreateAndRegister(*pImpl->vm, pImpl->policy);
    }
  }

  return *pImpl->vm;
}

const std::set<uint32_t>& WorldState::GetActorsByProfileId(
  int32_t profileId) const
{
  static const std::set<uint32_t> kEmptySet;

  auto it = actorIdByProfileId.find(profileId);
  if (it == actorIdByProfileId.end()) {
    return kEmptySet;
  }
  return it->second;
}

const std::set<uint32_t>& WorldState::GetActorsByPrivateIndexedProperty(
  const std::string& privateIndexedPropertyMapKey) const
{
  static const std::set<uint32_t> kEmptySet;

  auto it = actorIdByPrivateIndexedProperty.find(privateIndexedPropertyMapKey);
  if (it == actorIdByPrivateIndexedProperty.end()) {
    return kEmptySet;
  }
  return it->second;
}

std::string WorldState::MakePrivateIndexedPropertyMapKey(
  const std::string& propertyName, const std::string& propertyValueStringified)
{
  return propertyName + '=' + propertyValueStringified;
}

uint32_t WorldState::GenerateFormId()
{
  // Identity exists independently of admission. Looking it up can return null
  // when its saved NPC is cold or budget-denied; never reuse that durable ID.
  while (forms.count(pImpl->nextId) ||
         pImpl->changeFormsForDeferredLoad.count(pImpl->nextId) ||
         pImpl->npcTombstoneIds.count(pImpl->nextId)) {
    if (pImpl->nextId == UINT32_MAX)
      throw std::runtime_error("SRP_DYNAMIC_FORM_ID_SPACE_EXHAUSTED");
    ++pImpl->nextId;
  }
  if (pImpl->nextId < 0xff000000)
    throw std::runtime_error("SRP_DYNAMIC_FORM_ID_SPACE_EXHAUSTED");
  return pImpl->nextId++;
}

void WorldState::SetRelootTime(const std::string& recordType,
                               std::chrono::system_clock::duration time)
{
  auto it = std::find_if(pImpl->relootTimeForTypes.begin(),
                         pImpl->relootTimeForTypes.end(),
                         [&recordType](const RelootTimeForTypesEntry& entry) {
                           return entry.recordType == recordType;
                         });

  if (it != pImpl->relootTimeForTypes.end()) {
    it->time = time;
  } else {
    pImpl->relootTimeForTypes.push_back({ recordType, time });
  }
}

std::optional<std::chrono::system_clock::duration> WorldState::GetRelootTime(
  const std::string& recordType) const
{
  auto it = std::find_if(pImpl->relootTimeForTypes.begin(),
                         pImpl->relootTimeForTypes.end(),
                         [&recordType](const RelootTimeForTypesEntry& entry) {
                           return entry.recordType == recordType;
                         });

  if (it == pImpl->relootTimeForTypes.end()) {
    return std::nullopt;
  }
  return it->time;
}

bool WorldState::HasKeyword(uint32_t baseId, const char* keyword)
{
  auto lookupRes = GetEspm().GetBrowser().LookupById(baseId);

  if (!lookupRes.rec) {
    return false;
  }

  const auto keywordIds = lookupRes.rec->GetKeywordIds(GetEspmCache());

  for (auto keywordId : keywordIds) {
    auto keywordIdGlobal = lookupRes.ToGlobalId(keywordId);
    auto rec = GetEspm().GetBrowser().LookupById(keywordIdGlobal).rec;
    if (rec && !Utils::stricmp(rec->GetEditorId(GetEspmCache()), keyword)) {
      return true;
    }
  }

  return false;
}

bool WorldState::NpcSourceFilesOverriden() const noexcept
{
  return !npcSettings.empty() || defaultSetting.overriden;
}

bool WorldState::IsNpcAllowed(uint32_t refrId) const noexcept
{
  if (npcSettings.empty() && defaultSetting.overriden) {
    return true;
  }
  uint32_t npcFileIdx = GetFileIdx(refrId);
  for (const auto& [fileName, _] : npcSettings) {
    auto it = std::find(espmFiles.begin(), espmFiles.end(), fileName);
    if (it == espmFiles.end()) {
      return false;
    }
    auto idx = std::distance(espmFiles.begin(), it);
    if (npcFileIdx == idx) {
      return true;
    }
  }
  return false;
}

uint32_t WorldState::GetFileIdx(uint32_t formId) const noexcept
{
  return formId >> 24;
}

void WorldState::SetNpcSettings(
  std::unordered_map<std::string, NpcSettingsEntry>&& settings)
{
  npcSettings = settings;
}

bool WorldState::RemoveTimer(uint32_t timerId)
{
  return timerRegular.RemoveTimer(timerId);
}

bool WorldState::RemoveEffectTimer(uint32_t timerId)
{
  return timerEffects.RemoveTimer(timerId);
}

void WorldState::SetForbiddenRelootTypes(const std::set<std::string>& types)
{
  pImpl->forbiddenRelootTypes = types;
}

void WorldState::SetEnableConsoleCommandsForAllSetting(bool enable)
{
  enableConsoleCommandsForAll = enable;
}

bool WorldState::IsRelootForbidden(std::string type) const noexcept
{
  return pImpl->forbiddenRelootTypes.find(type) !=
    pImpl->forbiddenRelootTypes.end();
}

bool WorldState::HasEspmFile(std::string_view filename) const noexcept
{
  return std::find(espmFiles.begin(), espmFiles.end(), filename) !=
    espmFiles.end();
}
