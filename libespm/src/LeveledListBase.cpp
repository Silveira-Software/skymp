#include "libespm/LeveledListBase.h"
#include "libespm/ReadUnaligned.h"
#include "libespm/RecordHeaderAccess.h"
#include <cstring>

namespace espm {

LeveledListBase::Data LeveledListBase::GetData(
  CompressedFieldsCache& compressedFieldsCache) const noexcept
{
  Data result;
  RecordHeaderAccess::IterateFields(
    this,
    [&](const char* type, uint32_t dataSize, const char* data) {
      if (!std::memcmp(type, "EDID", 4)) {
        result.editorId = data;
      } else if (!std::memcmp(type, "LVLF", 4)) {
        result.leveledItemFlags = ReadFieldScalar<uint8_t>(data, dataSize);
      } else if (!std::memcmp(type, "LVLG", 4)) {
        result.chanceNoneGlobalId = ReadFieldScalar<uint32_t>(data, dataSize);
      } else if (!std::memcmp(type, "LVLD", 4)) {
        result.chanceNone = ReadFieldScalar<uint8_t>(data, dataSize);
      } else if (!std::memcmp(type, "LLCT", 4)) {
        result.numEntries = ReadFieldScalar<uint8_t>(data, dataSize);
        result.entries = reinterpret_cast<const Entry*>(data + 1);
      }
    },
    compressedFieldsCache);
  return result;
}

}
