#include "SrpCollision.h"
#include <catch2/catch_all.hpp>
#include <limits>
#include <fstream>
#include <cstdlib>
#include <cmath>

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

TEST_CASE("Observed R49 controller gap is inside the original Lord projectile radius", "[srp-r49-projectile-radius-real]")
{
  const auto path=std::getenv("SRP_R49_RADIUS_SCENE");
  if(!path){SKIP("Actual pinned scene is supplied by the isolated R49 build runner");}
  std::ifstream stream(path);nlohmann::json doc;stream>>doc;
  REQUIRE(Scene::Digest(doc)=="b8eb35fbf317d758b12b60e1ac22b2db61d81ca8a78dfe8c10e787289f66b6b4");
  const auto scene=Load(doc);
  const auto pose=[](Vec3 p,double degrees){
    const double a=degrees*std::acos(-1.)/180.,c=std::cos(a),s=std::sin(a);
    return std::array<double,12>{c,-s,0,p.x,s,c,0,p.y,0,0,1,p.z};
  };
  // Saved samples AFTER the close-range refusal, not an exact cast frame.
  const Vec3 player{369.1265563964844,10.46597957611084,-200.85382080078125};
  const Vec3 target{436.9781188964844,121.23995208740234,-175.3435821533203};
  const double yaw=35.30030059814453;
  const auto caster=scene->ActorTriangles("7:Skyrim.esm","283a:Dawnguard.esm",pose(player,yaw),false);
  const auto victim=scene->ActorTriangles("71aa:Dawnguard.esm","19fd3:Dawnguard.esm",pose(target,206.664306640625),false);
  REQUIRE(caster.has_value());REQUIRE(victim.has_value());
  double minZ=1e300,maxZ=-1e300;
  for(const auto& t:*caster)for(const auto& v:{t.a,t.b,t.c}){minZ=std::min(minZ,v.z);maxZ=std::max(maxZ,v.z);}
  const Vec3 from{player.x,player.y,minZ+.55*(maxZ-minZ)};
  const double a=yaw*std::acos(-1.)/180.;
  const Vec3 to{from.x+std::sin(a)*10000,from.y+std::cos(a)*10000,from.z};
  REQUIRE(IntersectMesh(from,to,*victim).verdict==Verdict::Clear);
  REQUIRE(SweepSphereMesh(from,to,21,*victim).verdict==Verdict::Clear);
  const auto swept=SweepSphereMesh(from,to,24,*victim);
  REQUIRE(swept.verdict==Verdict::Blocked);
  REQUIRE(swept.hitFraction>0);REQUIRE(swept.hitFraction<.02);
  Query query;query.worldOrCell=33580089;query.from=from;query.radius=24;
  query.to={from.x+(to.x-from.x)*swept.hitFraction,from.y+(to.y-from.y)*swept.hitFraction,from.z};
  query.dynamics.worldOrCell=query.worldOrCell;query.dynamics.revision=1;query.dynamics.complete=true;
  const auto trace=scene->Trace(query);
  INFO(trace.reason);REQUIRE(trace.verdict==Verdict::Clear);
}

TEST_CASE("Projectile spheres touch faces edges and vertices at their first contact", "[srp-r49-projectile-radius]")
{
  const std::vector<Triangle> mesh={{{5,0,0},{5,10,0},{5,0,10}}};
  auto hit=SweepSphereMesh({0,2,2},{10,2,2},1,mesh);
  REQUIRE(hit.verdict==Verdict::Blocked);
  REQUIRE(hit.hitFraction==Catch::Approx(.4).margin(1e-8));
  REQUIRE(SweepSphereMesh({0,2,2},{10000,2,2},1,mesh).hitFraction==Catch::Approx(.0004).margin(1e-8));
  REQUIRE(SweepSphereMesh({0,2,2},{10,2,2},0,mesh).hitFraction==Catch::Approx(.5));
  for(const auto offset:std::vector<Vec3>{{0,5,-1},{0,11,0}}){
    Vec3 to=offset;to.x=10;
    REQUIRE(IntersectMesh(offset,to,mesh).verdict==Verdict::Clear);
    REQUIRE(SweepSphereMesh(offset,to,.99,mesh).verdict==Verdict::Clear);
    hit=SweepSphereMesh(offset,to,1,mesh);
    REQUIRE(hit.verdict==Verdict::Blocked);
    REQUIRE(hit.hitFraction==Catch::Approx(.5).margin(1e-5));
  }
  REQUIRE(SweepSphereMesh({4.5,2,2},{10,2,2},1,mesh).hitFraction==0);
  REQUIRE(SweepSphereMesh({4.5,2,2},{4.5,2,2},1,mesh).verdict==Verdict::Blocked);
  REQUIRE(SweepSphereMesh({0,2,2},{3,2,2},1,mesh).verdict==Verdict::Clear);
  REQUIRE(SweepSphereMesh({0,2,2},{10,2,2},-1,mesh).verdict==Verdict::Unknown);
  REQUIRE(SweepSphereMesh({0,2,2},{10,2,2},std::numeric_limits<double>::quiet_NaN(),mesh).verdict==Verdict::Unknown);
  REQUIRE(SweepSphereMesh({0,2,2},{10,2,2},1,{}).verdict==Verdict::Unknown);
  auto invalid=mesh;invalid[0].c=invalid[0].a;
  REQUIRE(SweepSphereMesh({0,2,2},{10,2,2},1,invalid).verdict==Verdict::Unknown);
}

TEST_CASE("Projectile volume retains static dynamic and unknown collision guards", "[srp-r49-projectile-radius]")
{
  auto doc=Fixture();auto ray=Ray({0,5.5,0},{10,5.5,0});
  REQUIRE(Load(doc)->Trace(ray).verdict==Verdict::Clear);
  ray.radius=.6;
  REQUIRE(Load(doc)->Trace(ray).verdict==Verdict::Blocked);
  ray.radius=.49;
  REQUIRE(Load(doc)->Trace(ray).verdict==Verdict::Clear);
  auto point=Ray({5,5.5,0},{5,5.5,0});point.radius=.6;
  REQUIRE(Load(doc)->Trace(point).verdict==Verdict::Blocked);
  point.radius=.49;REQUIRE(Load(doc)->Trace(point).verdict==Verdict::Clear);
  ray.radius=.6;doc["geometry"]=nlohmann::json::array();
  ray.dynamics.triangles={{{5,-5,-5},{5,5,-5},{5,5,5}}};
  REQUIRE(Load(doc)->Trace(ray).verdict==Verdict::Blocked);
  ray.dynamics.triangles.clear();ray.dynamics.complete=false;
  REQUIRE(Load(doc)->Trace(ray).verdict==Verdict::Unknown);
  ray.dynamics.complete=true;
  ray.dynamics.unresolvedBlockers={{{4,6,-1},{6,7,1}}};
  REQUIRE(Load(doc)->Trace(ray).verdict==Verdict::Unknown);
  ray.dynamics.unresolvedBlockers.clear();
  doc["unresolved"].push_back({{"worldOrCell",60},{"min",{4,6,-1}},{"max",{6,7,1}}});
  REQUIRE(Load(doc)->Trace(ray).verdict==Verdict::Unknown);
  doc["unresolved"]=nlohmann::json::array();
  doc["coverage"].push_back({{"worldOrCell",60},{"min",{4,6,-1}},{"max",{6,7,1}},{"complete",false}});
  REQUIRE(Load(doc)->Trace(ray).verdict==Verdict::Unknown);
  doc["coverage"].erase(1);
  auto boundary=Ray({-9.8,10,10},{-8,10,10});
  REQUIRE(Load(doc)->Trace(boundary).verdict==Verdict::Clear);
  boundary.radius=.6;
  REQUIRE(Load(doc)->Trace(boundary).verdict==Verdict::Unknown);
  ray.radius=-1;REQUIRE(Load(doc)->Trace(ray).verdict==Verdict::Unknown);
  ray.radius=std::numeric_limits<double>::infinity();
  REQUIRE(Load(doc)->Trace(ray).verdict==Verdict::Unknown);
}

TEST_CASE("Projectile volume also expands existing physical spheres hulls and mesh margins", "[srp-r49-projectile-radius]")
{
  auto doc=Fixture();auto& g=doc["geometry"][0];g["representation"]="havok-physical-v2";
  g["margin"]=0;g["vertices"]=nlohmann::json::array();g["triangles"]=nlohmann::json::array();
  g["volumes"]=nlohmann::json::array({{{"kind","sphere"},{"center",{5,0,0}},{"radius",2}}});
  auto ray=Ray({0,2.5,0},{10,2.5,0});
  REQUIRE(Load(doc)->Trace(ray).verdict==Verdict::Clear);
  ray.radius=.6;REQUIRE(Load(doc)->Trace(ray).verdict==Verdict::Blocked);
  g["volumes"]=nlohmann::json::array({{{"kind","convex-envelope"},
    {"planes",{{1,0,0,-6},{-1,0,0,4},{0,1,0,-1},{0,-1,0,-1},{0,0,1,-1},{0,0,-1,-1}}},
    {"vertices",{{4,-1,-1},{6,1,1},{4,1,-1},{6,-1,1}}},{"boundingMargin",0}}});
  ray=Ray({0,1.5,0},{10,1.5,0});
  REQUIRE(Load(doc)->Trace(ray).verdict==Verdict::Clear);
  ray.radius=.6;REQUIRE(Load(doc)->Trace(ray).verdict==Verdict::Blocked);
  doc=Fixture();doc["geometry"][0]["representation"]="havok-physical-v2";
  doc["geometry"][0]["margin"]=.5;doc["geometry"][0]["volumes"]=nlohmann::json::array();
  ray=Ray({0,5.8,0},{10,5.8,0});
  REQUIRE(Load(doc)->Trace(ray).verdict==Verdict::Clear);
  ray.radius=.4;REQUIRE(Load(doc)->Trace(ray).verdict==Verdict::Blocked);
}

TEST_CASE("Native physical volumes block interiors and transformed Havok envelopes", "[srp-collision]")
{
  auto doc=Fixture();auto& g=doc["geometry"][0];
  g["representation"]="havok-physical-v2";g["margin"]=0;
  g["vertices"]=nlohmann::json::array();g["triangles"]=nlohmann::json::array();
  g["volumes"]=nlohmann::json::array({{{"kind","sphere"},{"center",{5,0,0}},{"radius",2}}});
  auto scene=Load(doc);
  REQUIRE(scene->Trace(Ray()).verdict==Verdict::Blocked);
  REQUIRE(scene->Trace(Ray({5,0,0},{5.1,0,0})).verdict==Verdict::Blocked);
  REQUIRE(scene->Trace(Ray({0,2,0},{10,2,0})).verdict==Verdict::Blocked);
  REQUIRE(scene->Trace(Ray({0,2.01,0},{10,2.01,0})).verdict==Verdict::Clear);
  g["worldTransform"]={0,-2,0,2,2,0,0,0,0,0,2,0};
  REQUIRE(Load(doc)->Trace(Ray({2,5,0},{2,15,0})).verdict==Verdict::Blocked);
  g["worldTransform"]={1,0,0,0,0,1,0,0,0,0,1,0};
  g["volumes"]=nlohmann::json::array({{{"kind","convex-envelope"},
    {"planes",{{1,0,0,-6},{-1,0,0,4},{0,1,0,-1},{0,-1,0,-1},{0,0,1,-1},{0,0,-1,-1}}},
    {"vertices",{{4,-1,-1},{6,1,1},{4,1,-1},{6,-1,1}}},{"boundingMargin",0}}});
  REQUIRE(Load(doc)->Trace(Ray()).verdict==Verdict::Blocked);
  REQUIRE(Load(doc)->Trace(Ray({5,0,0},{5.1,0,0})).verdict==Verdict::Blocked);
  REQUIRE(Load(doc)->Trace(Ray({0,1.01,0},{10,1.01,0})).verdict==Verdict::Clear);
  g["worldTransform"]={0,-2,0,2,2,0,0,0,0,0,2,0};
  REQUIRE(Load(doc)->Trace(Ray({2,5,0},{2,15,0})).verdict==Verdict::Blocked);
  REQUIRE(Load(doc)->Trace(Ray({4.01,5,0},{4.01,15,0})).verdict==Verdict::Clear);
  g["volumes"][0]["planes"][0][0]=0;
  REQUIRE_THROWS_WITH(Load(doc),"SRP_COLLISION_VOLUME_INVALID");
}

TEST_CASE("Havok mesh radius blocks grazing segments without enlarging actual actor hits", "[srp-collision]")
{
  auto doc=Fixture();auto& g=doc["geometry"][0];
  g["representation"]="havok-physical-v2";g["margin"]=.5;g["volumes"]=nlohmann::json::array();
  REQUIRE(Load(doc)->Trace(Ray({4.75,-2,0},{4.75,2,0})).verdict==Verdict::Blocked);
  REQUIRE(Load(doc)->Trace(Ray({4.49,-2,0},{4.49,2,0})).verdict==Verdict::Clear);
  REQUIRE(Load(doc)->Trace(Ray({0,5.25,0},{10,5.25,0})).verdict==Verdict::Blocked);
  REQUIRE(Load(doc)->Trace(Ray({0,5.51,0},{10,5.51,0})).verdict==Verdict::Clear);
  g["margin"]=-1;
  REQUIRE_THROWS_WITH(Load(doc),"SRP_COLLISION_MARGIN_INVALID");
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

TEST_CASE("Controller proxies bind sex and cannot shadow legacy geometry", "[srp-collision]")
{
  auto document=Fixture();
  auto shape=document.at("geometry").at(0);
  shape["baseDesc"]="7:Skyrim.esm";shape["raceDesc"]="13745:Skyrim.esm";
  shape["policy"]="SERVER_MESH_PROXY_V1";shape["representation"]="havok-controller-inscribed-v1";
  shape["localTransform"]={1,0,0,0,0,1,0,0,0,0,1,0};shape["sex"]=0;
  auto female=shape;female["sex"]=1;female["localTransform"][11]=10;
  document["actorShapes"]=nlohmann::json::array({shape,female});
  const auto scene=Load(document);
  const std::array<double,12> pose{1,0,0,0,0,1,0,0,0,0,1,0};
  REQUIRE_FALSE(scene->ActorTriangles("7:Skyrim.esm","13745:Skyrim.esm",pose).has_value());
  const auto maleMesh=scene->ActorTriangles("7:Skyrim.esm","13745:Skyrim.esm",pose,false);
  const auto femaleMesh=scene->ActorTriangles("7:Skyrim.esm","13745:Skyrim.esm",pose,true);
  REQUIRE(maleMesh.has_value());REQUIRE(femaleMesh.has_value());
  REQUIRE(femaleMesh->at(0).a.z-maleMesh->at(0).a.z==10);
  auto legacy=shape;legacy.erase("sex");legacy["representation"]="havok-triangles";
  document["actorShapes"].push_back(legacy);
  REQUIRE_THROWS_WITH(Load(document),"SRP_COLLISION_ACTOR_SHAPES_INVALID");
  document["actorShapes"]=nlohmann::json::array({shape,shape});
  REQUIRE_THROWS_WITH(Load(document),"SRP_COLLISION_ACTOR_SHAPES_INVALID");
  document["actorShapes"]=nlohmann::json::array({shape});
  document["actorShapes"][0]["sex"]=2;
  REQUIRE_THROWS_WITH(Load(document),"SRP_COLLISION_ACTOR_SHAPES_INVALID");
  document["actorShapes"][0]["sex"]=false;
  REQUIRE_THROWS_WITH(Load(document),"SRP_COLLISION_ACTOR_SHAPES_INVALID");
}
