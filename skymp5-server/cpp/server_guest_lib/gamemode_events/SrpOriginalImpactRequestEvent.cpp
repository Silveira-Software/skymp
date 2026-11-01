#include "SrpOriginalImpactRequestEvent.h"
#include <string>
#include <utility>

SrpOriginalImpactRequestEvent::SrpOriginalImpactRequestEvent(
  uint32_t userId_, uint32_t targetId_, std::string requestId_)
  : userId(userId_), targetId(targetId_), requestId(std::move(requestId_))
{
}

const char* SrpOriginalImpactRequestEvent::GetName() const
{
  return "onSrpOriginalImpactRequest";
}

std::string SrpOriginalImpactRequestEvent::GetArgumentsJsonArray() const
{
  // [userId, targetId, requestId] — requestId is a server-derived correlation
  // key; the gamemode passes it straight to beginSrpOriginalImpact without
  // trusting any client field.
  return "[" + std::to_string(userId) + "," + std::to_string(targetId) +
         ",\"" + requestId + "\"]";
}

void SrpOriginalImpactRequestEvent::OnFireSuccess(WorldState*)
{
}
