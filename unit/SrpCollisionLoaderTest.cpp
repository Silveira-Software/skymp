#include "SrpCollisionLoader.h"
#include <catch2/catch_all.hpp>
#include <fstream>
#include <unistd.h>

namespace {
// Isolated synthetic source files, not approved Skyrim coverage.
struct Fixture {
  std::filesystem::path root=std::filesystem::temp_directory_path()/
    ("srp-collision-loader-"+std::to_string(getpid()));
  nlohmann::json scene,config;
  Fixture(){
    std::filesystem::create_directories(root);
    for(auto name:{"fixture.esm","wall.nif","producer.py","audit.json"})
      std::ofstream(root/name)<<"abc";
    const std::string abc="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
    scene={{"schemaVersion",1},{"units","skyrim-world"},
      {"bindings",{{"loadOrder",nlohmann::json::array({{{"name","fixture.esm"},{"sha256",abc}}})},
        {"assets",nlohmann::json::array({{{"path","wall.nif"},{"sha256",abc}}})},
        {"producerSha256",abc},{"coverageAuditSha256",abc}}},
      {"geometry",nlohmann::json::array()},{"coverage",nlohmann::json::array()},
      {"unresolved",nlohmann::json::array()}};
    config={{"schemaVersion",1},{"enabled",true},{"scenePath",(root/"scene.json").string()},
      {"assetsRoot",root.string()},{"producerPath",(root/"producer.py").string()},
      {"coverageAuditPath",(root/"audit.json").string()}};Save();
  }
  void Save(){config["sceneSha256"]=SrpCollision::Scene::Digest(scene);std::ofstream(root/"scene.json")<<scene.dump();}
  SrpCollision::InstalledScene Load(){return SrpCollision::LoadServerScene(config,{root/"fixture.esm"});}
};
}
TEST_CASE("Collision startup derives provenance from actual source bytes", "[srp-collision-loader]")
{
  Fixture f;auto loaded=f.Load();REQUIRE(loaded.scene->Id()==f.config.at("sceneSha256").get<std::string>());
  REQUIRE(loaded.document==f.scene);
  for(auto name:{"fixture.esm","wall.nif","producer.py","audit.json"}){
    std::ofstream(f.root/name)<<"changed";
    REQUIRE_THROWS_WITH(f.Load(),"SRP_COLLISION_PROVENANCE_MISMATCH");
    std::ofstream(f.root/name)<<"abc";
  }
}
TEST_CASE("Collision startup refuses modified scene and wrong load order", "[srp-collision-loader]")
{
  Fixture f;std::ofstream(f.root/"scene.json")<<f.scene.dump();
  f.config["sceneSha256"]=std::string(64,'a');
  REQUIRE_THROWS_WITH(f.Load(),"SRP_COLLISION_CONTENT_MISMATCH");
  f.Save();
  REQUIRE_THROWS_WITH(SrpCollision::LoadServerScene(f.config,{}),"SRP_COLLISION_LOADORDER_MISMATCH");
  f.scene["bindings"]["loadOrder"][0]["name"]="different.esm";f.Save();
  REQUIRE_THROWS_WITH(f.Load(),"SRP_COLLISION_PROVENANCE_MISMATCH");
}
TEST_CASE("Collision startup never escapes asset root or accepts relative config paths", "[srp-collision-loader]")
{
  Fixture f;f.scene["bindings"]["assets"][0]["path"]="../wall.nif";f.Save();
  REQUIRE_THROWS_WITH(f.Load(),"SRP_COLLISION_ASSET_PATH_INVALID");
  f.config["assetsRoot"]="relative";
  REQUIRE_THROWS_WITH(f.Load(),"SRP_COLLISION_ABSOLUTE_PATH_REQUIRED");
  f.config["assetsRoot"]=f.root.string();f.config["enabled"]=false;
  REQUIRE_THROWS_WITH(f.Load(),"SRP_COLLISION_STARTUP_CONFIG_INVALID");
}

TEST_CASE("Collision coverage cannot certify an unenumerated cell with an empty audit", "[srp-collision-loader]")
{
  Fixture f;
  f.scene["coverage"] = nlohmann::json::array({{{"worldOrCell", 60},
    {"min", {-250000, -250000, -50000}}, {"max", {250000, 250000, 50000}}, {"complete", true}}});
  const nlohmann::json audit = {{"schemaVersion", 1}, {"cell", {{"formId", "3c"}}},
    {"pluginHashes", f.scene["bindings"]["loadOrder"]},
    {"producerSha256", f.scene["bindings"]["producerSha256"]},
    {"references", nlohmann::json::array()}, {"summary", {{"imported", 0}}}};
  std::ofstream(f.root / "audit.json") << audit.dump();
  f.scene["bindings"]["coverageAuditSha256"] = SrpCollision::Scene::Digest(audit);
  f.Save();
  REQUIRE_THROWS_WITH(f.Load(), "SRP_COLLISION_COVERAGE_AUDIT_GEOMETRY_MISMATCH");
}
