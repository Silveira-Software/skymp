#include "libespm/LIGH.h"
#include "libespm/ReadUnaligned.h"
#include "libespm/RecordHeaderAccess.h"

namespace espm {

LIGH::Data LIGH::GetData(CompressedFieldsCache& cache) const noexcept
{
  Data result;
  RecordHeaderAccess::IterateFields(
    this,
    [&](const char* type, uint32_t dataSize, const char* data) {
      if (!std::memcmp(type, "DATA", 4)) {
        result.data.time = ReadFieldScalar<int32_t>(data, dataSize);
        result.data.radius = ReadFieldScalar<uint32_t>(data, dataSize, 0x04);
        result.data.color = ReadFieldScalar<uint32_t>(data, dataSize, 0x08);
        result.data.flags = ReadFieldScalar<uint32_t>(data, dataSize, 0x0C);
        result.data.falloffExponent =
          ReadFieldScalar<float>(data, dataSize, 0x0F);
        result.data.fov = ReadFieldScalar<float>(data, dataSize, 0x14);
        result.data.nearClip = ReadFieldScalar<float>(data, dataSize, 0x18);
        result.data.frequency = ReadFieldScalar<float>(data, dataSize, 0x1C);
        result.data.intensityAmplitude =
          ReadFieldScalar<float>(data, dataSize, 0x1F);
        result.data.movementAmplitude =
          ReadFieldScalar<float>(data, dataSize, 0x24);
        result.data.value = ReadFieldScalar<uint32_t>(data, dataSize, 0x28);
        result.data.weight = ReadFieldScalar<float>(data, dataSize, 0x2C);
      }
    },
    cache);
  return result;
}

}
