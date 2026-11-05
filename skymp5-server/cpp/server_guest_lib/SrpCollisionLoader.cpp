#include "SrpCollisionLoader.h"
#include <algorithm>
#include <fstream>
#include <sodium.h>
#include <stdexcept>

namespace {
void Require(bool value,const char* error) { if(!value)throw std::runtime_error(error); }
std::string HashFile(const std::filesystem::path& path)
{
  std::ifstream file(path,std::ios::binary);Require(bool(file),"SRP_COLLISION_SOURCE_UNREADABLE");
  crypto_hash_sha256_state state;crypto_hash_sha256_init(&state);
  char buffer[65536];
  while(file.read(buffer,sizeof(buffer)) || file.gcount())
    crypto_hash_sha256_update(&state,reinterpret_cast<unsigned char*>(buffer),file.gcount());
  Require(file.eof(),"SRP_COLLISION_SOURCE_READ_FAILED");
  unsigned char digest[crypto_hash_sha256_BYTES];crypto_hash_sha256_final(&state,digest);
  constexpr char hex[]="0123456789abcdef";std::string result;
  for(auto b:digest){result+=hex[b>>4];result+=hex[b&15];}return result;
}
std::filesystem::path Absolute(const nlohmann::json& config,const char* key)
{
  const std::filesystem::path path=config.at(key).get<std::string>();
  Require(path.is_absolute(),"SRP_COLLISION_ABSOLUTE_PATH_REQUIRED");
  return std::filesystem::canonical(path);
}
void ValidateCoverageAudit(const nlohmann::json& document,
                           const std::filesystem::path& path)
{
  // Empty synthetic scenes make no gameplay claim. A claimed region must be
  // backed by the producer's actual reference audit, not a blanket rectangle.
  if (document.at("coverage").empty() && document.at("geometry").empty()) return;
  std::ifstream input(path);
  nlohmann::json audit;
  input >> audit;
  Require(audit.is_object() && audit.value("schemaVersion", 0) == 1 &&
    audit.contains("references") && audit.at("references").is_array() &&
    audit.contains("summary") && audit.at("summary").is_object() &&
    audit.contains("cell") && audit.at("cell").is_object(),
    "SRP_COLLISION_COVERAGE_AUDIT_REQUIRED");
  Require(audit.at("pluginHashes") == document.at("bindings").at("loadOrder") &&
    audit.at("producerSha256") == document.at("bindings").at("producerSha256"),
    "SRP_COLLISION_COVERAGE_AUDIT_SOURCE_MISMATCH");
  const auto cellText = audit.at("cell").at("formId").get<std::string>();
  size_t parsed = 0;
  const auto cell = std::stoul(cellText, &parsed, 16);
  Require(parsed == cellText.size() && cell > 0 && cell <= UINT32_MAX,
    "SRP_COLLISION_COVERAGE_AUDIT_CELL_INVALID");
  std::map<std::string, const nlohmann::json*> imported;
  for (const auto& row : audit.at("references")) {
    if (row.value("decision", std::string()) != "imported") continue;
    const auto ref = row.at("reference").get<std::string>();
    Require(imported.emplace(ref, &row).second,
      "SRP_COLLISION_COVERAGE_AUDIT_DUPLICATE_REF");
  }
  Require(!imported.empty() && imported.size() == document.at("geometry").size() &&
    audit.at("summary").at("imported") == imported.size(),
    "SRP_COLLISION_COVERAGE_AUDIT_GEOMETRY_MISMATCH");
  for (const auto& geometry : document.at("geometry")) {
    const auto ref = geometry.at("reference").get<std::string>();
    const auto found = imported.find(ref);
    Require(found != imported.end() && geometry.at("worldOrCell") == cell,
      "SRP_COLLISION_COVERAGE_AUDIT_REF_MISMATCH");
    const auto& row = *found->second;
    Require(row.at("base") == geometry.at("base") &&
      row.at("triangles") == geometry.at("triangles").size(),
      "SRP_COLLISION_COVERAGE_AUDIT_GEOMETRY_MISMATCH");
  }
  for (const auto& region : document.at("coverage"))
    Require(region.at("worldOrCell") == cell,
      "SRP_COLLISION_COVERAGE_AUDIT_REGION_MISMATCH");
}
}
SrpCollision::InstalledScene SrpCollision::LoadServerScene(const nlohmann::json& config,
  const std::vector<std::filesystem::path>& actualPluginPaths)
{
  Require(config.is_object() && config.at("schemaVersion")==1 && config.at("enabled")==true,
    "SRP_COLLISION_STARTUP_CONFIG_INVALID");
  const auto scenePath=Absolute(config,"scenePath"),root=Absolute(config,"assetsRoot");
  Require(std::filesystem::file_size(scenePath)<=64*1024*1024,"SRP_COLLISION_SCENE_SIZE_LIMIT");
  std::ifstream file(scenePath);nlohmann::json document;file>>document;
  const auto& declared=document.at("bindings");
  Require(actualPluginPaths.size()==declared.at("loadOrder").size(),"SRP_COLLISION_LOADORDER_MISMATCH");
  nlohmann::json bindings={{"loadOrder",nlohmann::json::array()},
    {"assets",nlohmann::json::array()},
    {"producerSha256",HashFile(Absolute(config,"producerPath"))},
    {"coverageAuditSha256",HashFile(Absolute(config,"coverageAuditPath"))}};
  for(const auto& path:actualPluginPaths)
    bindings["loadOrder"].push_back({{"name",path.filename().string()},{"sha256",HashFile(path)}});
  for(const auto& row:declared.at("assets")){
    const auto name=row.at("path").get<std::string>();
    // A producer that runs on Windows may emit backslashes, but this loader runs
    // on Linux where a backslash is a filename character, not a separator.
    // Normalise for filesystem resolution while keeping the declared string in
    // the bindings so provenance stays byte-exact.
    std::string portable=name;std::replace(portable.begin(),portable.end(),'\\','/');
    const std::filesystem::path relative=portable;
    Require(!relative.empty() && !relative.is_absolute() && !relative.has_root_name(),
      "SRP_COLLISION_ASSET_PATH_INVALID");
    for(const auto& part:relative)Require(part!="..","SRP_COLLISION_ASSET_PATH_INVALID");
    const auto path=std::filesystem::canonical(root/relative),inside=path.lexically_relative(root);
    Require(!inside.empty() && *inside.begin()!="..","SRP_COLLISION_ASSET_PATH_INVALID");
    bindings["assets"].push_back({{"path",name},{"sha256",HashFile(path)}});
  }
  auto scene = Scene::Load(document,bindings,config.at("sceneSha256").get<std::string>());
  ValidateCoverageAudit(document, Absolute(config, "coverageAuditPath"));
  return {std::move(scene),std::move(document)};
}
