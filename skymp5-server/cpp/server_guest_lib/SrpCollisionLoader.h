#pragma once
#include "SrpCollision.h"
#include <filesystem>

namespace SrpCollision {
struct InstalledScene {
  std::shared_ptr<const Scene> scene;
  nlohmann::json document;
};
// Startup only. No packet handler or client supplies these paths/pins. Hashes
// are recomputed from the actual server plugins and resolved loose assets.
InstalledScene LoadServerScene(const nlohmann::json& config,
  const std::vector<std::filesystem::path>& actualPluginPaths);
}
