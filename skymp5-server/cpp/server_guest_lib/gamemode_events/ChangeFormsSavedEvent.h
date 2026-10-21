#pragma once

#include <cstdint>

#include "GameModeEvent.h"

// SRP experimental: a save batch reached the database driver. The gamemode
// checks its pending forms with mp.isChangeFormSaved(formId, generation).
class ChangeFormsSavedEvent : public GameModeEvent
{
public:
  explicit ChangeFormsSavedEvent(uint64_t batchId_);

  const char* GetName() const override;

  std::string GetArgumentsJsonArray() const override;

private:
  void OnFireSuccess(WorldState*) override {}

  uint64_t batchId = 0;
};
