#pragma once
#include "NiPoint3.h"
#include "SrpCollision.h"
#include <Networking.h>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>

class MpActor;
class PartOne;

// Server-policy melee admission. This is not Skyrim animation/pose parity.
// Legacy OnHit/OnWeaponHit remain quarantined for managed originals.
namespace SrpOriginalImpact {
constexpr auto kProperty = "private.srpOriginalImpactV1";
constexpr auto kIdentityProperty = "private.srpImpactActorIdentityV1";
struct Frame {
  uint64_t revision = 0;
  std::string movementPolicy;
  uint32_t worldOrCell = 0;
  NiPoint3 attackerPosition, attackerAngle, targetPosition, targetAngle;
  std::shared_ptr<const SrpCollision::Scene> scene;
  SrpCollision::DynamicSnapshot dynamics;
  // Independently pinned, transformed target mesh, never OBND-derived proof.
  // Missing/unsupported pose representation cannot authorize an impact.
  std::vector<SrpCollision::Triangle> targetTriangles;
  std::string hitRepresentation;
};

// WorldState supplies Frame from its installed server-only scene and accepted
// native state. Neither endpoint accepts client damage, source, flags, a hit
// point, geometry, a collision boolean, numeric stats or a supplied Frame.
// requestId is only an untrusted idempotency key; the operationId is generated
// by this service, persisted, and never inferred from a legacy hit packet.
nlohmann::json Begin(PartOne& server, Networking::UserId userId,
                     uint32_t targetId, const std::string& requestId);
nlohmann::json Apply(PartOne& server, Networking::UserId userId,
                     const std::string& operationId);
nlohmann::json Cancel(PartOne& server, Networking::UserId userId,
                      const std::string& operationId);
nlohmann::json Inspect(const MpActor& actor);
}
