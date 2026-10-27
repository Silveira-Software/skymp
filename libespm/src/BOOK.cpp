#include "libespm/BOOK.h"
#include "libespm/ReadUnaligned.h"
#include "libespm/RecordHeaderAccess.h"
#include <cstring>

namespace espm {

bool BOOK::Data::IsFlagSet(const Flags flag) const noexcept
{
  return (flags & flag) == flag;
}

BOOK::Data BOOK::GetData(
  CompressedFieldsCache& compressedFieldsCache) const noexcept
{
  Data result;

  RecordHeaderAccess::IterateFields(
    this,
    [&](const char* type, uint32_t dataSize, const char* data) {
      if (!std::memcmp(type, "DATA", 4)) {
        result.flags =
          static_cast<Flags>(ReadFieldScalar<uint8_t>(data, dataSize));

        result.spellOrSkillFormId =
          ReadFieldScalar<uint32_t>(data, dataSize, 0x4);
        result.weight = ReadFieldScalar<float>(data, dataSize, 0xc);
      }
    },
    compressedFieldsCache);

  return result;
}

}
