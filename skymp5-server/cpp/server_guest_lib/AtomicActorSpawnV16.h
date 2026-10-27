#pragma once
#include "PartOne.h"
#include "MpActor.h"
#include "FormCallbacks.h"
#include <cmath>
#include <stdexcept>

// V16-only entry point. Every initial field is established before AddForm or
// subscription callbacks. Existing creation and restore paths are unchanged.
inline uint32_t PlaceActorAtV16Native(PartOne& part, uint32_t baseId,
                                    uint32_t worldId, const NiPoint3& pos,
                                    const NiPoint3& rot)
{
  const auto finite = [](const NiPoint3& v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
  };
  if (!finite(pos) || !finite(rot))
    throw std::runtime_error("v16_spawn_nonfinite_transform");
  auto& world = part.worldState;
  if (world.IsFormLoadingInProgress())
    throw std::runtime_error("v16_spawn_during_restore");
  const auto base = part.GetEspm().GetBrowser().LookupById(baseId);
  const auto cell = part.GetEspm().GetBrowser().LookupById(worldId);
  if (!base.rec || base.rec->GetType() != "NPC_")
    throw std::runtime_error("v16_spawn_invalid_npc_base");
  if (!cell.rec || (cell.rec->GetType() != "WRLD" && cell.rec->GetType() != "CELL"))
    throw std::runtime_error("v16_spawn_invalid_world_or_cell");
  const LocationalData loc{ pos, rot, FormDesc::FromFormId(worldId, world.espmFiles) };
  auto actor = std::make_unique<MpActor>(loc, part.CreateFormCallbacks(), baseId);
  actor->SetSpawnPoint(loc);
  const auto id = world.GenerateFormId();
  world.AddForm(std::move(actor), id);
  world.GetFormAt<MpActor>(id).ForceSubscriptionsUpdate();
  return id;
}
