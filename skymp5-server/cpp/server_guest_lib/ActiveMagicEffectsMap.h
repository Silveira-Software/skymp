#pragma once
#include "libespm/espm.h"
#include <chrono>
#include <nlohmann/json.hpp>
#include <optional>
#include <simdjson.h>
#include <unordered_map>

class ActiveMagicEffectsMap
{
public:
  struct Entry
  {
    uint32_t timerId = -1;
    espm::Effects::Effect data;
    std::chrono::system_clock::time_point endTime;
    // Absent on legacy rows. Never infer an old source from a matching MGEF.
    std::optional<nlohmann::json> provenance;
  };

public:
  static ActiveMagicEffectsMap FromJson(const simdjson::dom::element& effects);

public:
  template <typename T>
  void Add(espm::ActorValue actorValue, T&& entry)
  {
    stacked.erase(actorValue);
    effects[actorValue] = std::forward<T>(entry);
  }

  std::vector<espm::Effects::Effect> GetAllEffects() const noexcept;
  std::vector<Entry> GetEntries() const;
  static void ValidateProvenance(const nlohmann::json& value);
  static std::string StackKey(const Entry& entry);

  std::optional<std::reference_wrapper<const Entry>> Get(
    espm::ActorValue actorValue) const noexcept;
  // R37: distinct MGEF identities coexist; recast of one identity replaces it.
  void AddStacked(espm::ActorValue actorValue, Entry entry);
  std::optional<std::reference_wrapper<const Entry>> Get(espm::ActorValue actorValue,uint32_t effectId) const noexcept;
  void Remove(espm::ActorValue actorValue,uint32_t effectId) noexcept;
  std::optional<std::reference_wrapper<const Entry>> GetByStackKey(
    espm::ActorValue actorValue,const std::string& key) const noexcept;
  void RemoveByStackKey(espm::ActorValue actorValue,const std::string& key) noexcept;
  void Remove(espm::ActorValue actorValue) noexcept;
  void Clear() noexcept;
  bool Has(espm::ActorValue actorValue) const noexcept;
  [[nodiscard]] bool Empty() const noexcept;
  nlohmann::json::array_t ToJson() const;

private:
  std::unordered_map<espm::ActorValue, Entry> effects;
  std::unordered_map<espm::ActorValue,std::unordered_map<std::string,Entry>> stacked;
};
