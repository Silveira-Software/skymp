#include "libespm/RACE.h"
#include "libespm/ReadUnaligned.h"
#include "libespm/RecordHeaderAccess.h"
#include <cstring>

namespace espm {

RACE::Data RACE::GetData(
  CompressedFieldsCache& compressedFieldsCache) const noexcept
{
  Data result;
  RecordHeaderAccess::IterateFields(
    this,
    [&](const char* type, uint32_t dataSize, const char* data) {
      if (!std::memcmp(type, "DATA", 4)) {
        result.flags = ReadFieldScalar<uint32_t>(data, dataSize, 32);
        result.startingHealth = ReadFieldScalar<float>(data, dataSize, 36);
        result.startingMagicka = ReadFieldScalar<float>(data, dataSize, 40);
        result.startingStamina = ReadFieldScalar<float>(data, dataSize, 44);
        result.healRegen = ReadFieldScalar<float>(data, dataSize, 84);
        result.magickaRegen = ReadFieldScalar<float>(data, dataSize, 88);
        result.staminaRegen = ReadFieldScalar<float>(data, dataSize, 92);
        result.unarmedDamage = ReadFieldScalar<float>(data, dataSize, 96);
        result.unarmedReach = ReadFieldScalar<float>(data, dataSize, 100);
      } else if (!std::memcmp(type, "SPLO", 4)) {
        result.spells.emplace(ReadFieldScalar<uint32_t>(data, dataSize));
      }
    },
    compressedFieldsCache);
  return result;
}

}
