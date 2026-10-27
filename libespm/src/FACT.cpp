#include "libespm/FACT.h"
#include "libespm/ReadUnaligned.h"
#include "libespm/RecordHeaderAccess.h"
#include <cstring>

namespace espm {

FACT::Data FACT::GetData(
  CompressedFieldsCache& compressedFieldsCache) const noexcept
{
  Data result;
  RecordHeaderAccess::IterateFields(
    this,
    [&](const char* type, uint32_t dataSize, const char* data) {
      if (!std::memcmp(type, "FULL", 4)) {
        result.fullNameTableID = ReadFieldScalar<uint32_t>(data, dataSize);
      } else if (!std::memcmp(type, "XNAM", 4)) {
        const auto relation =
          ReadFieldScalar<InterfactionRelation>(data, dataSize);
        result.interfactionRelations.push_back(relation);
      } else if (!std::memcmp(type, "DATA", 4)) {
        result.flags = ReadFieldScalar<Flags>(data, dataSize);
      } else if (!std::memcmp(type, "JAIL", 4)) {
        result.prisonMarker = ReadFieldScalar<uint32_t>(data, dataSize);
      } else if (!std::memcmp(type, "WAIT", 4)) {
        result.followerWaitMarker = ReadFieldScalar<uint32_t>(data, dataSize);
      } else if (!std::memcmp(type, "STOL", 4)) {
        result.evidenceChest = ReadFieldScalar<uint32_t>(data, dataSize);
      } else if (!std::memcmp(type, "PLCN", 4)) {
        result.playerBelongingsChest =
          ReadFieldScalar<uint32_t>(data, dataSize);
      } else if (!std::memcmp(type, "CRGR", 4)) {
        result.crimeGroup = ReadFieldScalar<uint32_t>(data, dataSize);
      } else if (!std::memcmp(type, "JOUT", 4)) {
        result.jailOutfit = ReadFieldScalar<uint32_t>(data, dataSize);
      } else if (!std::memcmp(type, "CRVA", 4)) {
        result.crimeGold = ReadFieldScalar<CrimeGold>(data, dataSize);
      } else if (!std::memcmp(type, "RNAM", 4)) {
        result.ranks.push_back(espm::FACT::Rank());
        result.ranks.back().rankId = ReadFieldScalar<uint32_t>(data, dataSize);
      } else if (!std::memcmp(type, "MNAM", 4)) {
        result.ranks.back().maleTitle =
          ReadFieldScalar<uint32_t>(data, dataSize);
      } else if (!std::memcmp(type, "FNAM", 4)) {
        result.ranks.back().femaleTitle =
          ReadFieldScalar<uint32_t>(data, dataSize);
      } else if (!std::memcmp(type, "CTDA", 4)) {
        result.conditions.push_back(ReadFieldScalar<CTDA>(data, dataSize));
      }
    },
    compressedFieldsCache);
  return result;
}

}
