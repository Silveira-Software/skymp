#include "libespm/MISC.h"
#include "libespm/ReadUnaligned.h"
#include "libespm/CompressedFieldsCache.h"
#include "libespm/RecordHeaderAccess.h"

namespace espm {

MISC::Data MISC::GetData(CompressedFieldsCache& cache) const
{
  Data res;
  RecordHeaderAccess::IterateFields(
    this,
    [&](const char* type, uint32_t dataSize, const char* data) {
      if (!std::memcmp(type, "DATA", 4)) {
        res.weight = ReadFieldScalar<float>(data, dataSize, 0x4);
      }
    },
    cache);
  return res;
}

}
