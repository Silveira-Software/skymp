#include "libespm/MGEF.h"
#include "libespm/ReadUnaligned.h"
#include "libespm/RecordHeaderAccess.h"
#include <cstring>
#include <type_traits>

namespace espm {

MGEF::Data MGEF::GetData(
  CompressedFieldsCache& compressedFieldsCache) const noexcept
{
  Data result;
  RecordHeaderAccess::IterateFields(
    this,
    [&](const char* type, uint32_t dataSize, const char* data) {
      if (!std::memcmp(type, "DATA", 4)) {
        result.data.flags = ReadFieldScalar<Flags>(data, dataSize);
        result.data.effectType = EffectType{
          ReadFieldScalar<std::underlying_type_t<EffectType>>(data, dataSize, 0x40)
        };
        result.data.primaryAV = ActorValue(
          ReadFieldScalar<std::underlying_type_t<ActorValue>>(data, dataSize, 0x44));
      }
    },
    compressedFieldsCache);

  return result;
}

}
