#include "libespm/Effects.h"
#include "libespm/ReadUnaligned.h"
#include "libespm/RecordHeaderAccess.h"
#include <cstring>
#include <fmt/format.h>

namespace espm {

Effects::Effects()
{
}

Effects::Effects(const RecordHeader* parent)
  : parent(parent)
{
}

Effects::Data Effects::GetData(
  CompressedFieldsCache& compressedFieldsCache) const noexcept
{
  if (!parent) {
    return Data();
  }

  Data result;
  std::size_t effectIndex = 0;
  bool orderFlag = true;

  RecordHeaderAccess::IterateFields(
    parent,
    [&](const char* type, uint32_t dataSize, const char* data) {
      if (!std::memcmp(type, "EFID", 4)) {
        if (!orderFlag || effectIndex != result.effects.size()) {
          auto name = parent->GetEditorId(compressedFieldsCache);
          throw std::runtime_error(
            fmt::format("Bad effect array for edid={}", name));
        }
        const auto effectId = ReadFieldScalar<uint32_t>(data, dataSize);
        result.effects.emplace_back();
        result.effects.back().effectId = effectId;
        orderFlag = false;
      } else if (!std::memcmp(type, "EFIT", 4)) {
        if (orderFlag || effectIndex >= result.effects.size()) {
          auto name = parent->GetEditorId(compressedFieldsCache);
          throw std::runtime_error(
            fmt::format("Bad effect array for edid={}", name));
        }
        const auto magnitude = ReadFieldScalar<float>(data, dataSize);
        const auto areaOfEffect =
          ReadFieldScalar<uint32_t>(data, dataSize, 4);
        const auto duration = ReadFieldScalar<uint32_t>(data, dataSize, 8);
        Effect& eff = result.effects[effectIndex];
        eff.magnitude = magnitude;
        eff.areaOfEffect = areaOfEffect;
        eff.duration = duration;
        ++effectIndex;
        orderFlag = true;
      }
    },
    compressedFieldsCache);
  return result;
}

}
