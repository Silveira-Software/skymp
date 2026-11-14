#include "SrpCollisionLoader.h"
#include <catch2/catch_all.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>

// End-to-end validation with a scene produced from the shipped game assets.
// The bundle is mounted at SRP_SCENE_DIR (scene.json, audit.json, producer.py,
// assets/) and SRP_SCENE_PLUGIN locates the directory of installed plugins.
// Without them the case skips: the synthetic cases cannot certify real geometry.
namespace {
std::filesystem::path EnvPath(const char* name)
{
  const char* value = std::getenv(name);
  return value ? std::filesystem::path(value) : std::filesystem::path();
}

nlohmann::json ReadJson(const std::filesystem::path& path)
{
  std::ifstream file(path);
  REQUIRE(file.good());
  nlohmann::json document;
  file >> document;
  return document;
}

SrpCollision::Vec3 Apply(const nlohmann::json& matrix, const nlohmann::json& vertex)
{
  const auto m = matrix.get<std::array<double, 12>>();
  const auto v = vertex.get<std::array<double, 3>>();
  return { m[0] * v[0] + m[1] * v[1] + m[2] * v[2] + m[3],
           m[4] * v[0] + m[5] * v[1] + m[6] * v[2] + m[7],
           m[8] * v[0] + m[9] * v[1] + m[10] * v[2] + m[11] };
}
}

TEST_CASE("A produced scene loads through the audited loader and blocks", "[srp-collision-loader]")
{
  const auto dir = EnvPath("SRP_SCENE_DIR");
  const auto plugin = EnvPath("SRP_SCENE_PLUGIN");
  if (dir.empty() || plugin.empty())
    SKIP("SRP_SCENE_DIR/SRP_SCENE_PLUGIN unset: producer output not mounted");

  const auto document = ReadJson(dir / "scene.json");
  // The pin an operator records out of band: the digest of the document exactly
  // as nlohmann re-serialises it. This is what a Python producer cannot compute.
  const auto digest = SrpCollision::Scene::Digest(document);
  REQUIRE(digest.size() == 64);

  const nlohmann::json config = {
    { "schemaVersion", 1 },
    { "enabled", true },
    { "scenePath", (dir / "scene.json").string() },
    { "assetsRoot", (dir / "assets").string() },
    { "producerPath", (dir / "producer.py").string() },
    { "coverageAuditPath", (dir / "audit.json").string() },
    { "sceneSha256", digest }
  };
  std::vector<std::filesystem::path> plugins;
  for (const auto& entry : document.at("bindings").at("loadOrder"))
    plugins.push_back(plugin.parent_path() / entry.at("name").get<std::string>());
  const auto installed = SrpCollision::LoadServerScene(config, plugins);

  // Provenance came from the actual plugin and asset bytes, and the scene is
  // the one whose digest we just computed.
  REQUIRE(installed.scene->Id() == digest);
  REQUIRE(installed.document.at("geometry").size() > 0);
  REQUIRE(installed.scene->References().size() > 0);

  // Actual pinned controller geometry must survive the native reader with the
  // same identity, sex, height transform and triangle count. This is physical
  // asset integration, not an assertion about the current Skyrim animation.
  if (installed.document.contains("actorShapes")) {
    const std::array<double,12> pose{1,0,0,0,0,1,0,0,0,0,1,0};
    for (const auto& shape : installed.document.at("actorShapes")) {
      if (shape.at("representation") != "havok-controller-inscribed-v1") continue;
      const auto base=shape.at("baseDesc").get<std::string>();
      const auto race=shape.at("raceDesc").get<std::string>();
      REQUIRE_FALSE(installed.scene->ActorTriangles(base,race,pose).has_value());
      const auto mesh=installed.scene->ActorTriangles(base,race,pose,shape.at("sex").get<int>()==1);
      REQUIRE(mesh.has_value());
      REQUIRE(mesh->size()==shape.at("triangles").size());
      const auto& t=mesh->at(0);
      const SrpCollision::Vec3 center{(t.a.x+t.b.x+t.c.x)/3.,(t.a.y+t.b.y+t.c.y)/3.,(t.a.z+t.b.z+t.c.z)/3.};
      const SrpCollision::Vec3 u{t.b.x-t.a.x,t.b.y-t.a.y,t.b.z-t.a.z};
      const SrpCollision::Vec3 v{t.c.x-t.a.x,t.c.y-t.a.y,t.c.z-t.a.z};
      SrpCollision::Vec3 n{u.y*v.z-u.z*v.y,u.z*v.x-u.x*v.z,u.x*v.y-u.y*v.x};
      const auto length=std::sqrt(n.x*n.x+n.y*n.y+n.z*n.z);
      REQUIRE(length>0);
      n={n.x/length,n.y/length,n.z/length};
      REQUIRE(SrpCollision::IntersectMesh({center.x-n.x,center.y-n.y,center.z-n.z},
        {center.x+n.x,center.y+n.y,center.z+n.z},*mesh).verdict==SrpCollision::Verdict::Blocked);
    }
  }

  const auto& rows = installed.document.at("geometry");
  const auto triangleBatch = std::find_if(rows.begin(), rows.end(),
    [](const auto& row) { return !row.at("triangles").empty(); });
  REQUIRE(triangleBatch != rows.end());
  const auto& geometry = *triangleBatch;
  const auto& vertices = geometry.at("vertices");
  const auto& index = geometry.at("triangles").at(0).get<std::array<int, 3>>();
  REQUIRE(vertices.size() >= 3);
  const SrpCollision::Vec3 a = Apply(geometry.at("worldTransform"), vertices.at(index[0]));
  const SrpCollision::Vec3 b = Apply(geometry.at("worldTransform"), vertices.at(index[1]));
  const SrpCollision::Vec3 c = Apply(geometry.at("worldTransform"), vertices.at(index[2]));
  const SrpCollision::Vec3 centre{ (a.x + b.x + c.x) / 3.0, (a.y + b.y + c.y) / 3.0,
                                   (a.z + b.z + c.z) / 3.0 };
  const SrpCollision::Vec3 e1{ b.x - a.x, b.y - a.y, b.z - a.z };
  const SrpCollision::Vec3 e2{ c.x - a.x, c.y - a.y, c.z - a.z };
  const SrpCollision::Vec3 normal{ e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z,
                                   e1.x * e2.y - e1.y * e2.x };
  const double length = std::sqrt(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
  REQUIRE(length > 1e-6);

  const auto cell = geometry.at("worldOrCell").get<uint32_t>();
  SrpCollision::Query hit;
  hit.worldOrCell = cell;
  hit.from = { centre.x + normal.x / length * 150.0, centre.y + normal.y / length * 150.0,
               centre.z + normal.z / length * 150.0 };
  hit.to = centre;
  const auto blocked = installed.scene->Trace(hit);
  REQUIRE(blocked.verdict == SrpCollision::Verdict::Blocked);
  REQUIRE(blocked.reason == "SRP_COLLISION_STATIC_BLOCKED");

  // Outside the audited coverage the same scene must refuse to prove anything.
  SrpCollision::Query outside;
  outside.worldOrCell = cell;
  outside.from = { centre.x, centre.y, centre.z + 5000.0 };
  outside.to = { centre.x, centre.y, centre.z + 4000.0 };
  REQUIRE(installed.scene->Trace(outside).verdict == SrpCollision::Verdict::Unknown);
}
