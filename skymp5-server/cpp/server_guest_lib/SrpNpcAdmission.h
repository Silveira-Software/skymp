#pragma once
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <string>

// Main-world-thread admission, reserved BEFORE Init/grid/script activation.
// Counts loaded NPC identities (including corpses/disabled actors), not
// players.
class SrpNpcAdmission
{
public:
  void Configure(const nlohmann::json& value);
  bool Enabled() const { return enabled; }
  bool HasLease(uint32_t id) const { return leases.count(id); }
  bool Controlled(const std::string& reference) const;
  bool ControlsPlugin(const std::string& plugin) const;
  void Reserve(uint32_t id, const std::string& reference,
               const std::string& base, const std::string& region, bool player,
               const std::string& sourcePlugin = "");
  bool Move(uint32_t id, const std::string& region);
  void Release(uint32_t id) { leases.erase(id); }
  void ResetLeases() { leases.clear(); }
  nlohmann::json Inspect() const;

private:
  struct Lease
  {
    std::string category, region;
  };
  struct Source
  {
    std::string category;
    std::set<std::string> references;
  };
  bool enabled = false;
  uint32_t totalLimit = 0, faunaLimit = 0, otherLimit = 0, regionLimit = 0;
  std::map<std::string, Source> sources;
  std::set<std::string> faunaBases;
  std::map<uint32_t, Lease> leases;
};
