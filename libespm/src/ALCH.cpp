#include "libespm/ALCH.h"
#include "libespm/ReadUnaligned.h"
#include "libespm/RecordHeaderAccess.h"
#include <cstring>

namespace espm {

ALCH::Data ALCH::GetData(
  CompressedFieldsCache& compressedFieldsCache) const noexcept
{
  Data result;
  result.effects = Effects(this).GetData(compressedFieldsCache).effects;
  RecordHeaderAccess::IterateFields(
    this,
    [&](const char* type, uint32_t dataSize, const char* data) {
      if (!std::memcmp(type, "DATA", 4)) {
        result.weight = ReadFieldScalar<float>(data, dataSize);
      }
      if (!std::memcmp(type, "ENIT", 4)) {
        const auto flags = ReadFieldScalar<uint32_t>(data, dataSize, 0x4);
        // Other flags are not used in Skyrim
        result.isFood = flags & 0x00002;
        result.isPoison = flags & 0x20000;
      }
    },
    compressedFieldsCache);
  return result;
}

}
