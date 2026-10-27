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

  std::optional<std::reference_wrapper<const Entry>> Get(
    espm::ActorValue actorValue) const noexcept;
  // R37: distinct MGEF identities coexist; recast of one identity replaces it.
  void AddStacked(espm::ActorValue actorValue, Entry entry);
  std::optional<std::reference_wrapper<const Entry>> Get(espm::ActorValue actorValue,uint32_t effectId) const noexcept;
  void Remove(espm::ActorValue actorValue,uint32_t effectId) noexcept;
  void Remove(espm::ActorValue actorValue) noexcept;
  void Clear() noexcept;
  bool Has(espm::ActorValue actorValue) const noexcept;
  [[nodiscard]] bool Empty() const noexcept;
  nlohmann::json::array_t ToJson() const;

private:
  std::unordered_map<espm::ActorValue, Entry> effects;
  std::unordered_map<espm::ActorValue,std::unordered_map<uint32_t,Entry>> stacked;
};
