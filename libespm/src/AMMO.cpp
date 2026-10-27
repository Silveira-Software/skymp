#include "libespm/AMMO.h"
#include "libespm/ReadUnaligned.h"
#include "libespm/RecordHeaderAccess.h"

namespace espm {

AMMO::Data AMMO::GetData(CompressedFieldsCache& cache) const
{
  Data res;
  RecordHeaderAccess::IterateFields(
    this,
    [&](const char* type, uint32_t dataSize, const char* data) {
      if (!std::memcmp(type, "DATA", 4)) {
        // NOTE: 0x10 offset is for SSE version only
        res.weight = ReadFieldScalar<float>(data, dataSize, 0x10);
      }
    },
    cache);
  return res;
}

}
