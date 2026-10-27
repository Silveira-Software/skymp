#include "libespm/ARMO.h"
#include "libespm/ReadUnaligned.h"
#include "libespm/RecordHeaderAccess.h"
#include <cstring>

namespace espm {

ARMO::Data ARMO::GetData(CompressedFieldsCache& compressedFieldsCache) const
{
  Data result;
  bool hasDNAM = false;
  RecordHeaderAccess::IterateFields(
    this,
    [&](const char* type, uint32_t dataSize, const char* data) {
      if (!std::memcmp(type, "EITM", 4)) {
        result.enchantmentFormId = ReadFieldScalar<uint32_t>(data, dataSize);
      } else if (!std::memcmp(type, "DATA", 4)) {
        result.baseValue = ReadFieldScalar<uint32_t>(data, dataSize);
        result.weight = ReadFieldScalar<float>(data, dataSize, 4);
      } else if (!std::memcmp(type, "DNAM", 4)) {
        hasDNAM = true;
        result.baseRatingX100 = ReadFieldScalar<uint32_t>(data, dataSize);
      } else if (!std::memcmp(type, "ETYP", 4)) {
        result.equipSlotId = ReadFieldScalar<uint32_t>(data, dataSize);
      } else if (!std::memcmp(type, "BODT", 4) && dataSize >= 8) {
        result.bodt.present = true;
        result.bodt.bodyPartFlags = ReadFieldScalar<uint32_t>(data, dataSize);
        result.bodt.skill = ReadFieldScalar<uint32_t>(data, dataSize, 4);
      } else if (!std::memcmp(type, "BOD2", 4) && dataSize >= 8) {
        result.bod2.present = true;
        result.bod2.bodyPartFlags = ReadFieldScalar<uint32_t>(data, dataSize);
        result.bod2.skill = ReadFieldScalar<uint32_t>(data, dataSize, 4);
      }
    },
    compressedFieldsCache);
  if (!hasDNAM) {
    throw std::runtime_error("bad record ARMO? DNAM was not found");
  }
  return result;
}

}
