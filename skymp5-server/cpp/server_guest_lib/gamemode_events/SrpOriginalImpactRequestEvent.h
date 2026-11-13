#pragma once
#include "GameModeEvent.h"
#include <cstdint>
#include <string>

// Fired by ActionListener::OnHit when an authenticated player attacks a
// managed original actor. It carries only the admission request; the impact
// service still decides reach, collision, proof, damage and death. The packet
// that triggered it is not a proof of contact.
class SrpOriginalImpactRequestEvent : public GameModeEvent
{
public:
  SrpOriginalImpactRequestEvent(uint32_t userId_, uint32_t targetId_,
                                std::string requestId_, uint32_t spellId_ = 0);

  const char* GetName() const override;
  std::string GetArgumentsJsonArray() const override;

private:
  void OnFireSuccess(WorldState* worldState) override;

  uint32_t userId;
  uint32_t targetId;
  std::string requestId;
  uint32_t spellId;
};
