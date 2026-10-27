#include "libespm/FLOR.h"
#include "libespm/ReadUnaligned.h"
#include "libespm/RecordHeaderAccess.h"
#include <cstring>

namespace espm {

FLOR::Data FLOR::GetData(
  CompressedFieldsCache& compressedFieldsCache) const noexcept
{
  Data result;
  RecordHeaderAccess::IterateFields(
    this,
    [&](const char* type, uint32_t dataSize, const char* data) {
      if (!std::memcmp(type, "EDID", 4))
        result.editorId = data;
      else if (!std::memcmp(type, "FULL", 4))
        result.fullName = data;
      else if (!std::memcmp(type, "OBND", 4))
        result.bounds = reinterpret_cast<const ObjectBounds*>(data);
      else if (!std::memcmp(type, "PFIG", 4))
        result.resultItem = ReadFieldScalar<uint32_t>(data, dataSize);
      else if (!std::memcmp(type, "SNAM", 4))
        result.useSound = ReadFieldScalar<uint32_t>(data, dataSize);
    },
    compressedFieldsCache);
  return result;
}

}
