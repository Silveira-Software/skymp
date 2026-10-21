#pragma once

#include <cstdint>
#include <string>

#include "GameModeEvent.h"

class MpActor;

class EatItemEvent : public GameModeEvent
{
public:
  EatItemEvent(MpActor* actor_, uint32_t baseId_, bool isIngredient_,
               bool isAlchemyItem_, std::string opId_ = {});

  const char* GetName() const override;

  std::string GetArgumentsJsonArray() const override;

private:
  void OnFireSuccess(WorldState* worldState) override;

  // event arguments
  MpActor* actor = nullptr;
  uint32_t baseId = 0;
  std::string opId; // SRP experimental; empty for plain OnEquip

  // OnFireSuccess-specific arguments
  bool isIngredient = false;
  bool isAlchemyItem = false;
};
