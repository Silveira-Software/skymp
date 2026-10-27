#include "libespm/WRLD.h"
#include "libespm/ReadUnaligned.h"

#include <algorithm>

#include "libespm/RecordHeaderAccess.h"
#include <algorithm>

namespace espm {

WRLD::Data WRLD::GetData(CompressedFieldsCache& cache) const noexcept
{
  Data result;
  RecordHeaderAccess::IterateFields(
    this,
    [&](const char* type, uint32_t dataSize, const char* data) {
      if (!std::memcmp(type, "FULL", 4)) {
        result.localNameIndex = ReadFieldScalar<lstring>(data, dataSize);
      } else if (!std::memcmp(type, "WCTR", 4)) {
        std::copy_n(data, std::size(result.centerCellXY), result.centerCellXY);
      } else if (!std::memcmp(type, "LTMP", 4)) {
        result.interiorLightingId = ReadFieldScalar<formId>(data, dataSize);
      } else if (!std::memcmp(type, "XEZN", 4)) {
        result.encounterZoneId = ReadFieldScalar<formId>(data, dataSize);
      } else if (!std::memcmp(type, "XLCN", 4)) {
        result.exitLocationId = ReadFieldScalar<formId>(data, dataSize);
      } else if (!std::memcmp(type, "CNAM", 4)) {
        result.climateId = ReadFieldScalar<formId>(data, dataSize);
      } else if (!std::memcmp(type, "NAM2", 4)) {
        result.waterId = ReadFieldScalar<formId>(data, dataSize);
      } else if (!std::memcmp(type, "NAM3", 4)) {
        result.waterLODTypeId = ReadFieldScalar<formId>(data, dataSize);
      } else if (!std::memcmp(type, "NAM4", 4)) {
        result.waterLODHeight = ReadFieldScalar<float>(data, dataSize);
      } else if (!std::memcmp(type, "DNAM", 4)) {
        std::copy_n(data, std::size(result.LandData), result.LandData);
      } else if (!std::memcmp(type, "MODL", 4)) {
        result.cloudModelFileName = reinterpret_cast<const char*>(data);
      } else if (!std::memcmp(type, "MNAM", 4)) {
        if (dataSize == sizeof(WRLD::MapData::MapSize)) {
          result.mapData.mapSize =
            ReadFieldScalar<WRLD::MapData::MapSize>(data, dataSize);
        } else {
          result.mapData = ReadFieldScalar<WRLD::MapData>(data, dataSize);
        }
      } else if (!std::memcmp(type, "NAMA", 4)) {
        result.distantLODMult = ReadFieldScalar<float>(data, dataSize);
      } else if (!std::memcmp(type, "DATA", 4)) {
        result.flags = ReadFieldScalar<WRLD::Flags>(data, dataSize);
      } else if (!std::memcmp(type, "WNAM", 4)) {
        result.parentWorldspaceId = ReadFieldScalar<formId>(data, dataSize);
      } else if (!std::memcmp(type, "PNAM", 4)) {
        result.useFlags = ReadFieldScalar<WRLD::UseParentFlags>(data, dataSize);
      } else if (!std::memcmp(type, "ONAM", 4)) {
        std::copy_n(data, std::size(result.mapMarkerData),
                    result.mapMarkerData);
      } else if (!std::memcmp(type, "TNAM", 4)) {
        result.diffuseLODName = reinterpret_cast<const char*>(data);
      } else if (!std::memcmp(type, "UNAM", 4)) {
        result.normalLODName = reinterpret_cast<const char*>(data);
      } else if (!std::memcmp(type, "ZNAM", 4)) {
        result.musicId = ReadFieldScalar<formId>(data, dataSize);
      } else if (!std::memcmp(type, "XWEM", 4)) {
        result.waterEnvMapName = reinterpret_cast<const char*>(data);
      }
    },
    cache);
  return result;
}

}
