#include "EatItemEvent.h"

#include "MpActor.h"
#include "WorldState.h"
#include "SrpActorModifiers.h"
#include <nlohmann/json.hpp>
#include <unordered_set>
#include <vector>

EatItemEvent::EatItemEvent(MpActor* actor_, uint32_t baseId_,
                           bool isIngredient_, bool isAlchemyItem_,
                           std::string opId_)
  : actor(actor_)
  , baseId(baseId_)
  , opId(std::move(opId_))
  , isIngredient(isIngredient_)
  , isAlchemyItem(isAlchemyItem_)
{
}

const char* EatItemEvent::GetName() const
{
  return "onEatItem";
}

std::string EatItemEvent::GetArgumentsJsonArray() const
{
  std::string result;
  result += "[";
  result += std::to_string(actor->GetFormId());
  result += ",";
  result += std::to_string(baseId);
  if (!opId.empty()) {
    // opId is limited to 128 chars by ActionListener; JSON-escape it anyway
    result += ",";
    result += nlohmann::json(opId).dump();
  }
  result += "]";
  return result;
}

void EatItemEvent::OnFireSuccess(WorldState* worldState)
{
  if(isAlchemyItem&&SrpActorModifiers::Managed(*actor)){
    const bool sweet=worldState->HasEspmFile("SweetPie.esp");
    SrpActorModifiers::ApplyFromSource(*actor,baseId,*actor,opId,sweet);
    return;
  }
  std::vector<espm::Effects::Effect> effects;
  if (isAlchemyItem) {
    effects = espm::GetData<espm::ALCH>(baseId, worldState).effects;
  } else if (isIngredient) {
    // effects = espm::GetData<espm::INGR>(baseId, worldState).effects;
  } else {
    return;
  }
  std::unordered_set<std::string> modFiles = { worldState->espmFiles.begin(),
                                               worldState->espmFiles.end() };
  bool hasSweetpie = modFiles.count("SweetPie.esp");
  // Effect form IDs belong to the winning ALCH record's file/master table,
  // not the server load order. Overrides may have a different owner than baseId.
  const auto owner = worldState->GetEspm().GetBrowser().LookupById(baseId);
  for (auto& effect : effects) {
    effect.effectId = owner.ToGlobalId(effect.effectId);
  }
  actor->ApplyMagicEffects(effects, hasSweetpie);
}
