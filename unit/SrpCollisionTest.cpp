#include "SrpCollision.h"
#include <catch2/catch_all.hpp>
#include <limits>

using namespace SrpCollision;
namespace {
// Synthetic isolated collision fixtures. These contain no Bethesda geometry,
// and passing them does not certify an actual game-world coverage producer.
nlohmann::json Fixture()
{
  const std::string hash(64,'a');
  return {{"schemaVersion",1},{"units","skyrim-world"},
    {"bindings",{{"loadOrder",nlohmann::json::array({{{"name","fixture.esm"},{"sha256",hash}}})},
      {"assets",nlohmann::json::array({{{"path","fixture-wall.nif"},{"sha256",hash}}})},
      {"producerSha256",hash},{"coverageAuditSha256",hash}}},
    {"geometry",nlohmann::json::array({{
      {"worldOrCell",60},{"reference","fixture-wall:fixture.esm"},
      {"base","FixtureWall:fixture.esm"},
      {"asset","fixture-wall.nif"},{"representation","havok-triangles"},
      {"worldTransform",{1,0,0,0,0,1,0,0,0,0,1,0}},
      {"referenceTransform",{1,0,0,0,0,1,0,0,0,0,1,0}},
      {"vertices",{{5,-5,-5},{5,5,-5},{5,5,5},{5,-5,5}}},
      {"triangles",{{0,1,2},{0,2,3}}}}})},
    {"coverage",nlohmann::json::array({{{"worldOrCell",60},{"min",{-10,-10,-10}},
      {"max",{20,20,20}},{"complete",true}}})},
    {"unresolved",nlohmann::json::array()}};
}
std::shared_ptr<const Scene> Load(nlohmann::json document)
{
  return Scene::Load(document,document.at("bindings"),Scene::Digest(document));
}
Query Ray(Vec3 from={0,0,0},Vec3 to={10,0,0})
{
  Query q;q.worldOrCell=60;q.from=from;q.to=to;
  q.dynamics.worldOrCell=60;q.dynamics.revision=7;q.dynamics.complete=true;return q;
}
}

TEST_CASE("Native collision intersects physical triangles in both directions", "[srp-collision]")
{
  const auto scene=Load(Fixture());
  auto result=scene->Trace(Ray());
  REQUIRE(result.verdict==Verdict::Blocked);
  REQUIRE(result.reason=="SRP_COLLISION_STATIC_BLOCKED");
  REQUIRE(result.hitFraction==.5);
  REQUIRE(result.sceneId==scene->Id());REQUIRE(result.dynamicRevision==7);
  REQUIRE(scene->Trace(Ray({10,0,0},{0,0,0})).verdict==Verdict::Blocked);
  REQUIRE(scene->Trace(Ray({0,6,0},{10,6,0})).verdict==Verdict::Clear);
  REQUIRE(scene->Trace(Ray({0,5,5},{10,5,5})).verdict==Verdict::Blocked);
  REQUIRE(scene->Trace(Ray({5,0,0},{6,0,0})).verdict==Verdict::Blocked);
  REQUIRE(scene->Trace(Ray({5,-9,0},{5,9,0})).verdict==Verdict::Blocked);
  REQUIRE(scene->Trace(Ray({5,6,6},{5,7,7})).verdict==Verdict::Clear);
}

TEST_CASE("Native collision transforms imported physical vertices", "[srp-collision]")
{
  auto document=Fixture();
  // Rotate around Z and translate X. The X=5 wall becomes the Y=5 wall.
  document["geometry"][0]["worldTransform"]={0,-1,0,12,1,0,0,0,0,0,1,0};
  const auto scene=Load(document);
  REQUIRE(scene->Trace(Ray()).verdict==Verdict::Clear);
  REQUIRE(scene->Trace(Ray({12,0,0},{12,10,0})).verdict==Verdict::Blocked);
}

TEST_CASE("Native collision never infers coverage from endpoints or missing data", "[srp-collision]")
{
  auto document=Fixture();document["geometry"]=nlohmann::json::array();
  document["coverage"]=nlohmann::json::array({
    {{"worldOrCell",60},{"min",{-1,-1,-1}},{"max",{3,1,1}},{"complete",true}},
    {{"worldOrCell",60},{"min",{7,-1,-1}},{"max",{11,1,1}},{"complete",true}}});
  REQUIRE(Load(document)->Trace(Ray()).verdict==Verdict::Unknown);
  document["coverage"][1]["min"]={3,-1,-1};
  REQUIRE(Load(document)->Trace(Ray()).verdict==Verdict::Clear);
  document["coverage"][1]["min"]={3.000000001,-1,-1};
  REQUIRE(Load(document)->Trace(Ray()).verdict==Verdict::Unknown);
  document["coverage"][1]["min"]={3,-1,-1};
  document["coverage"][1]["complete"]=false;
  REQUIRE(Load(document)->Trace(Ray()).verdict==Verdict::Unknown);
  document["coverage"]=nlohmann::json::array();
  REQUIRE(Load(document)->Trace(Ray()).verdict==Verdict::Unknown);
  auto q=Ray();q.worldOrCell=61;
  REQUIRE(Load(Fixture())->Trace(q).verdict==Verdict::Unknown);
}

TEST_CASE("Native collision requires a complete server dynamic blocker snapshot", "[srp-collision]")
{
  const auto scene=Load(Fixture());auto q=Ray({0,6,0},{10,6,0});
  q.dynamics.complete=false;REQUIRE(scene->Trace(q).verdict==Verdict::Unknown);
  q.dynamics.complete=true;q.dynamics.revision=0;
  REQUIRE(scene->Trace(q).verdict==Verdict::Unknown);
  q.dynamics.revision=8;q.dynamics.worldOrCell=61;
  REQUIRE(scene->Trace(q).verdict==Verdict::Unknown);
  q.dynamics.worldOrCell=60;
  q.dynamics.triangles.push_back({{2,5,-1},{2,7,-1},{2,6,1}});
  REQUIRE(scene->Trace(q).verdict==Verdict::Blocked);
  REQUIRE(scene->Trace(q).reason=="SRP_COLLISION_DYNAMIC_BLOCKED");
  q.dynamics.triangles.clear();q.dynamics.unresolvedBlockers.push_back({{1,5,-1},{3,7,1}});
  REQUIRE(scene->Trace(q).verdict==Verdict::Unknown);
  q.dynamics.unresolvedBlockers[0]={{1,8,-1},{3,9,1}};
  REQUIRE(scene->Trace(q).verdict==Verdict::Clear);
  q.dynamics.unresolvedBlockers[0].min.x=std::numeric_limits<double>::quiet_NaN();
  REQUIRE(scene->Trace(q).verdict==Verdict::Unknown);
  q.dynamics.unresolvedBlockers.clear();q.dynamics.triangles.push_back({{1,1,1},{1,1,1},{1,1,1}});
  REQUIRE(scene->Trace(q).verdict==Verdict::Unknown);
}

TEST_CASE("Unsupported static geometry intersects as unknown and cannot prove clear", "[srp-collision]")
{
  auto document=Fixture();document["unresolved"].push_back({
    {"worldOrCell",60},{"min",{2,5,-1}},{"max",{3,7,1}},{"reason","unsupported bhk shape"}});
  const auto scene=Load(document);
  REQUIRE(scene->Trace(Ray({0,6,0},{10,6,0})).verdict==Verdict::Unknown);
  REQUIRE(scene->Trace(Ray({0,8,0},{10,8,0})).verdict==Verdict::Clear);
  // A known physical intersection remains blocked even with incomplete coverage.
  auto q=Ray();q.dynamics.complete=false;
  REQUIRE(scene->Trace(q).verdict==Verdict::Blocked);
}

TEST_CASE("Native collision validates immutable provenance and rejects geometry stand-ins", "[srp-collision]")
{
  auto document=Fixture();const auto digest=Scene::Digest(document);
  auto binding=document.at("bindings");binding["loadOrder"][0]["sha256"]=std::string(64,'b');
  REQUIRE_THROWS_WITH(Scene::Load(document,binding,digest),"SRP_COLLISION_PROVENANCE_MISMATCH");
  document["geometry"][0]["vertices"][0][0]=6;
  REQUIRE_THROWS_WITH(Scene::Load(document,document.at("bindings"),digest),"SRP_COLLISION_CONTENT_MISMATCH");
  document=Fixture();document["geometry"][0]["representation"]="OBND";
  REQUIRE_THROWS_WITH(Load(document),"SRP_COLLISION_GEOMETRY_INVALID");
  document=Fixture();document["geometry"][0]["asset"]="unknown.nif";
  REQUIRE_THROWS_WITH(Load(document),"SRP_COLLISION_GEOMETRY_INVALID");
  document=Fixture();document["geometry"][0]["triangles"][0]={0,0,1};
  REQUIRE_THROWS_WITH(Load(document),"SRP_COLLISION_TRIANGLE_INVALID");
  document=Fixture();document["geometry"][0]["triangles"][0]={0,1,-1};
  REQUIRE_THROWS_WITH(Load(document),"SRP_COLLISION_TRIANGLE_INVALID");
  document=Fixture();document["geometry"][0]["triangles"][0]={0,1,100};
  REQUIRE_THROWS_WITH(Load(document),"SRP_COLLISION_TRIANGLE_INVALID");
  document=Fixture();document["geometry"][0]["worldTransform"][0]=0;
  REQUIRE_THROWS_WITH(Load(document),"SRP_COLLISION_TRANSFORM_INVALID");
  document=Fixture();document["bindings"]["assets"][0]["sha256"]="not a hash";
  REQUIRE_THROWS_WITH(Load(document),"SRP_COLLISION_ASSET_INVALID");
}

TEST_CASE("Native collision rejects nonfinite and zero length queries", "[srp-collision]")
{
  const auto scene=Load(Fixture());auto q=Ray();q.from.x=std::numeric_limits<double>::infinity();
  REQUIRE(scene->Trace(q).verdict==Verdict::Unknown);
  q=Ray();q.to=q.from;REQUIRE(scene->Trace(q).verdict==Verdict::Unknown);
  q=Ray();q.worldOrCell=0;REQUIRE(scene->Trace(q).verdict==Verdict::Unknown);
}
