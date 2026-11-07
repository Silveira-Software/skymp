#pragma once
#include <nlohmann/json.hpp>
#include <cstdint>
#include <array>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// This module accepts only a server-installed, content-pinned scene. It has no
// packet/raycast input and never infers empty space from missing geometry.
namespace SrpCollision {
struct Vec3 { double x = 0, y = 0, z = 0; };
struct Triangle { Vec3 a, b, c; };
struct Aabb { Vec3 min, max; };
enum class Verdict { Unknown, Clear, Blocked };

struct DynamicSnapshot {
  uint32_t worldOrCell = 0;
  uint64_t revision = 0;
  // Set only by a server producer that enumerated every relevant live blocker
  // at the accepted simulation revision (doors, movable bodies, spawned refs).
  bool complete = false;
  std::vector<Triangle> triangles;
  std::vector<Aabb> unresolvedBlockers;
};
struct Query {
  uint32_t worldOrCell = 0;
  Vec3 from, to;
  DynamicSnapshot dynamics;
};
struct Result {
  Verdict verdict = Verdict::Unknown;
  std::string reason;
  std::string sceneId;
  uint64_t dynamicRevision = 0;
  double hitFraction = 1;
};
// Clear means the segment missed every triangle; Blocked means a physical
// surface intersection. Empty, malformed and nonfinite inputs yield Unknown.
Result IntersectMesh(Vec3 from, Vec3 to, const std::vector<Triangle>& triangles);

struct ReferenceBinding {
  std::string reference, base, asset;
  uint32_t worldOrCell = 0;
  std::array<double,12> referenceTransform;
};

class Scene final {
public:
  // expectedBindings must have been independently derived from the active
  // server load order/plugin bytes and resolved collision asset bytes. The
  // digest is the SHA256 of document.dump(), not an unverified document field.
  // Server code must pin it out-of-band; a client must never select either.
  static std::shared_ptr<const Scene> Load(
    const nlohmann::json& document,
    const nlohmann::json& expectedBindings,
    const std::string& expectedDocumentSha256);
  static std::string Digest(const nlohmann::json& document);
  Result Trace(const Query& query) const;
  const std::string& Id() const { return id; }
  const nlohmann::json& Bindings() const { return bindings; }
  const std::vector<ReferenceBinding>& References() const { return references; }
  // An explicit server policy proxy, made from fixed physical mesh data. This
  // does not claim the animated client's current Havok body pose. Missing or
  // unsupported actor collision templates return no mesh and must deny impact.
  std::optional<std::vector<Triangle>> ActorTriangles(
    const std::string& baseDesc, const std::string& raceDesc,
    const std::array<double,12>& serverPose,
    std::optional<bool> female = std::nullopt) const;

private:
  Scene() = default;
  struct Batch {
    uint32_t worldOrCell;
    Aabb bounds;
    std::vector<Triangle> triangles;
    double margin = 0;
    struct Volume {
      std::vector<std::array<double,4>> planes;
      Vec3 center;
      double radius = 0;
    };
    std::vector<Volume> volumes;
  };
  struct Region {
    uint32_t worldOrCell;
    Aabb bounds;
    bool complete;
  };
  struct ActorShape {
    std::string baseDesc, raceDesc;
    std::vector<Triangle> triangles;
    std::optional<bool> female;
  };
  std::string id;
  nlohmann::json bindings;
  std::vector<Batch> batches;
  std::vector<Region> regions;
  std::vector<std::pair<uint32_t, Aabb>> unresolved;
  std::vector<ReferenceBinding> references;
  std::vector<ActorShape> actorShapes;
};
}
