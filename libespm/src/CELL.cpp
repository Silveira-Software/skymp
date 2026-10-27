#include "libespm/CELL.h"
#include "libespm/ReadUnaligned.h"
#include "libespm/RecordHeaderAccess.h"

namespace espm {

CELL::Data CELL::GetData(CompressedFieldsCache& cache) const noexcept
{
  Data result;
  RecordHeaderAccess::IterateFields(
    this,
    [&](const char* type, uint32_t dataSize, const char* data) {
      if (!std::memcmp(type, "DATA", 4)) {
        // TODO: support size == 1, docs says it is possible in vanila skyrim
        result.flags = ReadFieldScalar<uint16_t>(data, dataSize);
      }
    },
    cache);
  return result;
}

}
