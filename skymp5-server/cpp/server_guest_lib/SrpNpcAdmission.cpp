#include "SrpNpcAdmission.h"
#include <algorithm>
#include <cctype>
#include <stdexcept>
namespace {
std::string Canonical(std::string s)
{
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return s;
}
std::string PluginName(const std::string& name)
{
  const auto value = Canonical(name);
  const auto dot = value.rfind('.');
  if (value.empty() || value.find_first_of(":/\\") != std::string::npos ||
      dot == std::string::npos || dot == 0 ||
      (value.substr(dot) != ".esm" && value.substr(dot) != ".esp" &&
       value.substr(dot) != ".esl"))
    throw std::runtime_error("SRP_NPC_ADMISSION_PLUGIN_INVALID");
  return value;
}
std::string Plugin(const std::string& desc)
{
  const auto p = desc.find(':');
  if (p == std::string::npos || p == 0 || p > 6 ||
      !std::all_of(desc.begin(), desc.begin() + p,
                   [](unsigned char c) { return std::isxdigit(c); }))
    throw std::runtime_error("SRP_NPC_ADMISSION_DESCRIPTOR_INVALID");
  return PluginName(desc.substr(p + 1));
}
uint32_t Limit(const nlohmann::json& j, const char* k, uint32_t max)
{
  if (!j.contains(k) || !j.at(k).is_number_integer())
    throw std::runtime_error("SRP_NPC_ADMISSION_LIMIT_INVALID");
  auto v = j.at(k).get<int64_t>();
  if (v < 1 || v > max)
    throw std::runtime_error("SRP_NPC_ADMISSION_LIMIT_INVALID");
  return v;
}
}
void SrpNpcAdmission::Configure(const nlohmann::json& j)
{
  if (!leases.empty())
    throw std::runtime_error("SRP_NPC_ADMISSION_RECONFIGURE_WITH_LEASES");
  if (!j.is_object() || j.size() != 5 || !j.contains("schemaVersion") ||
      !j.at("schemaVersion").is_number_integer() ||
      j.at("schemaVersion") != 1 || !j.contains("enabled") ||
      !j.at("enabled").is_boolean() || !j.contains("limits") ||
      !j.at("limits").is_object() || j.at("limits").size() != 4 ||
      !j.contains("sources") || !j.at("sources").is_array() ||
      !j.contains("faunaBases") || !j.at("faunaBases").is_array())
    throw std::runtime_error("SRP_NPC_ADMISSION_CONFIG_INVALID");
  SrpNpcAdmission next;
  next.enabled = j.at("enabled");
  const auto& limits = j.at("limits");
  next.totalLimit = Limit(limits, "total", 300);
  next.faunaLimit = Limit(limits, "fauna", 180);
  next.otherLimit = Limit(limits, "other", 120);
  next.regionLimit = Limit(limits, "perRegion", 300);
  for (const auto& row : j.at("sources")) {
    if (!row.is_object() || row.size() != 3 || !row.contains("plugin") ||
        !row.at("plugin").is_string() || !row.contains("category") ||
        !row.at("category").is_string() || !row.contains("references") ||
        !row.at("references").is_array())
      throw std::runtime_error("SRP_NPC_ADMISSION_SOURCE_INVALID");
    const auto plugin = PluginName(row.at("plugin").get<std::string>()),
               category = row.at("category").get<std::string>();
    if (plugin.empty() || (category != "fauna" && category != "other"))
      throw std::runtime_error("SRP_NPC_ADMISSION_CATEGORY_INVALID");
    Source source{ category, {} };
    for (const auto& r : row.at("references")) {
      const auto d = Canonical(r.get<std::string>());
      Plugin(d);
      if (!source.references.insert(d).second)
        throw std::runtime_error("SRP_NPC_ADMISSION_SOURCE_REFERENCE_INVALID");
    }
    if (!next.sources.emplace(plugin, std::move(source)).second)
      throw std::runtime_error("SRP_NPC_ADMISSION_SOURCE_DUPLICATE");
  }
  for (const auto& b : j.at("faunaBases")) {
    auto desc = Canonical(b.get<std::string>());
    Plugin(desc);
    if (!next.faunaBases.insert(desc).second)
      throw std::runtime_error("SRP_NPC_ADMISSION_BASE_DUPLICATE");
  }
  *this = std::move(next);
}
bool SrpNpcAdmission::Controlled(const std::string& reference) const
{
  return enabled && sources.count(Plugin(reference));
}
bool SrpNpcAdmission::ControlsPlugin(const std::string& plugin) const
{
  return enabled && sources.count(Canonical(plugin));
}
void SrpNpcAdmission::Reserve(uint32_t id, const std::string& reference,
                              const std::string& base,
                              const std::string& region, bool player,
                              const std::string& sourcePlugin)
{
  if (!enabled || player)
    return;
  if (leases.count(id))
    throw std::runtime_error("SRP_NPC_ADMISSION_DUPLICATE");
  const auto r = Canonical(reference);
  auto source = sources.find(sourcePlugin.empty()
                               ? (id >= 0xff000000 ? std::string() : Plugin(r))
                               : Canonical(sourcePlugin));
  if (source != sources.end() && !source->second.references.count(r))
    throw std::runtime_error("SRP_NPC_ADMISSION_REFERENCE_NOT_APPROVED");
  const auto category = source != sources.end() ? source->second.category
    : faunaBases.count(Canonical(base))         ? "fauna"
                                                : "other";
  uint32_t count = 0, zone = 0;
  for (const auto& [_, lease] : leases) {
    count += lease.category == category;
    zone += lease.region == region;
  }
  if (leases.size() >= totalLimit)
    throw std::runtime_error("SRP_NPC_ADMISSION_TOTAL_LIMIT");
  if (count >= (category == "fauna" ? faunaLimit : otherLimit))
    throw std::runtime_error("SRP_NPC_ADMISSION_CATEGORY_LIMIT");
  if (zone >= regionLimit)
    throw std::runtime_error("SRP_NPC_ADMISSION_REGION_LIMIT");
  leases.emplace(id, Lease{ category, region });
}
bool SrpNpcAdmission::Move(uint32_t id, const std::string& region)
{
  auto current = leases.find(id);
  if (current == leases.end() || current->second.region == region)
    return false;
  uint32_t count = 0;
  for (const auto& [_, lease] : leases)
    count += lease.region == region;
  if (count >= regionLimit)
    throw std::runtime_error("SRP_NPC_ADMISSION_REGION_LIMIT");
  current->second.region = region;
  return true;
}
nlohmann::json SrpNpcAdmission::Inspect() const
{
  nlohmann::json regions = nlohmann::json::object();
  uint32_t fauna = 0, other = 0;
  for (const auto& [_, lease] : leases) {
    if (lease.category == "fauna")
      ++fauna;
    else
      ++other;
    regions[lease.region] = regions.value(lease.region, 0) + 1;
  }
  return { { "schemaVersion", 1 },
           { "enabled", enabled },
           { "total", leases.size() },
           { "fauna", fauna },
           { "other", other },
           { "regions", regions },
           { "limits",
             { { "total", totalLimit },
               { "fauna", faunaLimit },
               { "other", otherLimit },
               { "perRegion", regionLimit } } },
           { "authority", "NATIVE_LOADED_NPC_ADMISSION_NOT_COMBAT" } };
}
