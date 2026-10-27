#include "libespm/REFR.h"
#include "libespm/ReadUnaligned.h"
#include "libespm/RecordHeaderAccess.h"

namespace espm {

REFR::Data REFR::GetData(
  CompressedFieldsCache& compressedFieldsCache) const
{
  Data result;
  espm::RecordHeaderAccess::IterateFields(
    this,
    [&](const char* type, uint32_t dataSize, const char* data) {
      if (!std::memcmp(type, "NAME", 4)) {
        result.baseId = ReadFieldScalar<uint32_t>(data, dataSize);
      } else if (!std::memcmp(type, "XSCL", 4)) {
        result.scale = ReadFieldScalar<float>(data, dataSize);
      } else if (!std::memcmp(type, "DATA", 4)) {
        if (dataSize < sizeof(LocationalData)) {
          throw std::runtime_error("espm: truncated REFR locational data");
        }
        result.loc = reinterpret_cast<const LocationalData*>(data);
      } else if (!std::memcmp(type, "XTEL", 4)) {
        if (dataSize < sizeof(DoorTeleport)) {
          throw std::runtime_error("espm: truncated REFR door teleport");
        }
        result.teleport = reinterpret_cast<const DoorTeleport*>(data);
      } else if (!std::memcmp(type, "XPRM", 4)) {
        auto bounds = std::make_shared<std::array<float, 3>>(
          ReadFieldScalar<std::array<float, 3>>(data, dataSize));
        result.boundsDiv2Storage = bounds;
        result.boundsDiv2 = bounds->data();
      } else if (!std::memcmp(type, "XCNT", 4)) {
        result.count = ReadFieldScalar<uint32_t>(data, dataSize);
      } else if (!std::memcmp(type, "XAPD", 4)) {
        result.isParentActivationOnly =
          ReadFieldScalar<uint8_t>(data, dataSize);
      } else if (!std::memcmp(type, "XAPR", 4)) {
        ActivationParentInfo info;
        info = ReadFieldScalar<ActivationParentInfo>(data, dataSize);
        result.activationParents.push_back(info);
      } else if (!std::memcmp(type, "XLKR", 4)) {
        [[likely]] if (dataSize == 8) {
          result.linkedRefKeywordId = ReadFieldScalar<uint32_t>(data, dataSize);
          result.linkedRefId = ReadFieldScalar<uint32_t>(data, dataSize, 4);
        } else if (dataSize == 4) {
          result.linkedRefId = ReadFieldScalar<uint32_t>(data, dataSize);
        }
      } else if (!std::memcmp(type, "XOWN", 4)) {
        result.ownerFaction = ReadFieldScalar<uint32_t>(data, dataSize);
      } else if (!std::memcmp(type, "FULL", 4)) {
        result.mapMarkerName = ReadFieldScalar<lstring>(data, dataSize);
      } else if (!std::memcmp(type, "FNAM", 4)) {
        result.mapMarkerFlags = ReadFieldScalar<MapMarkerFlags>(data, dataSize);
      } else if (!std::memcmp(type, "TNAM", 4)) {
        result.mapMarkerType = ReadFieldScalar<MapMarkerType>(data, dataSize);
      }
    },
    compressedFieldsCache);
  return result;
}

}
