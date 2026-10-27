#include "libespm/COBJ.h"
#include "libespm/ReadUnaligned.h"
#include "libespm/RecordHeaderAccess.h"
#include <cstring>

namespace espm {

COBJ::Data COBJ::GetData(
  CompressedFieldsCache& compressedFieldsCache) const noexcept
{
  Data result;
  RecordHeaderAccess::IterateFields(
    this,
    [&](const char* type, uint32_t dataSize, const char* data) {
      if (!std::memcmp(type, "CNTO", 4)) {
        result.inputObjects.push_back(
          ReadFieldScalar<InputObject>(data, dataSize));
      } else if (!std::memcmp(type, "CNAM", 4)) {
        const auto formId = ReadFieldScalar<uint32_t>(data, dataSize);
        result.outputObjectFormId = formId;
      } else if (!std::memcmp(type, "BNAM", 4)) {
        const auto formId = ReadFieldScalar<uint32_t>(data, dataSize);
        result.benchKeywordId = formId;
      } else if (!std::memcmp(type, "NAM1", 4)) {
        const auto count = ReadFieldScalar<uint16_t>(data, dataSize);
        result.outputCount = count;
      } else if (!std::memcmp(type, "CTDA", 4)) {
        result.conditions.push_back(ReadFieldScalar<CTDA>(data, dataSize));
      }
    },
    compressedFieldsCache);
  return result;
}

}
