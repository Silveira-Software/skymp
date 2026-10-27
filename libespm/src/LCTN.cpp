#include "libespm/LCTN.h"
#include "libespm/ReadUnaligned.h"
#include "libespm/RecordHeaderAccess.h"
#include <cstring>

namespace espm {
namespace {

template <class T>
void AppendArray(std::vector<T>& out, uint32_t dataSize,
                 const char* data) noexcept
{
  static_assert(std::is_trivially_copyable_v<T>);
  if (dataSize % sizeof(T) != 0) {
    throw std::runtime_error("espm: truncated location array");
  }
  const auto count = dataSize / sizeof(T);
  const auto oldSize = out.size();
  out.resize(oldSize + count);
  if (count != 0) {
    std::memcpy(out.data() + oldSize, data, dataSize);
  }
}

void AppendEncounterRef(std::vector<LCTN::EncounterRef>& out,
                        uint32_t dataSize, const char* data) noexcept
{
  if (dataSize < sizeof(formId)) {
    return;
  }

  LCTN::EncounterRef encounterRef;
  encounterRef.worldId = ReadFieldScalar<formId>(data, dataSize);

  const auto pairsSize = dataSize - sizeof(formId);
  const auto pairsCount = pairsSize / sizeof(LCTN::CellGrid);
  const auto pairs =
    reinterpret_cast<const LCTN::CellGrid*>(data + sizeof(formId));
  encounterRef.cellGrids.insert(encounterRef.cellGrids.end(), pairs,
                                pairs + pairsCount);

  out.push_back(encounterRef);
}

}

LCTN::Data LCTN::GetData(
  CompressedFieldsCache& compressedFieldsCache) const noexcept
{
  Data result;
  RecordHeaderAccess::IterateFields(
    this,
    [&](const char* type, uint32_t dataSize, const char* data) {
      if (!std::memcmp(type, "ACPR", 4) || !std::memcmp(type, "LCPR", 4)) {
        AppendArray(result.populationRefs, dataSize, data);
      } else if (!std::memcmp(type, "RCPR", 4)) {
        AppendArray(result.actorRefs, dataSize, data);
      } else if (!std::memcmp(type, "ACUN", 4) ||
                 !std::memcmp(type, "LCUN", 4)) {
        AppendArray(result.uniqueRefs, dataSize, data);
      } else if (!std::memcmp(type, "ACSR", 4) ||
                 !std::memcmp(type, "LCSR", 4)) {
        AppendArray(result.staticRefs, dataSize, data);
      } else if (!std::memcmp(type, "ACEC", 4) ||
                 !std::memcmp(type, "LCEC", 4)) {
        AppendEncounterRef(result.encounterRefs, dataSize, data);
      } else if (!std::memcmp(type, "ACEP", 4) ||
                 !std::memcmp(type, "LCEP", 4)) {
        AppendArray(result.enablePoints, dataSize, data);
      } else if (!std::memcmp(type, "ACID", 4) ||
                 !std::memcmp(type, "LCID", 4)) {
        AppendArray(result.unknownRefs, dataSize, data);
      } else if (!std::memcmp(type, "FULL", 4)) {
        result.fullNameTableID = ReadFieldScalar<lstring>(data, dataSize);
      } else if (!std::memcmp(type, "KSIZ", 4)) {
        result.keywordsCount = ReadFieldScalar<uint32_t>(data, dataSize);
      } else if (!std::memcmp(type, "KWDA", 4)) {
        AppendArray(result.keywordIds, dataSize, data);
      } else if (!std::memcmp(type, "PNAM", 4)) {
        result.parentLocationId = ReadFieldScalar<formId>(data, dataSize);
      } else if (!std::memcmp(type, "NAM1", 4)) {
        result.musicId = ReadFieldScalar<formId>(data, dataSize);
      } else if (!std::memcmp(type, "FNAM", 4)) {
        result.unreportedCrimeFactionId =
          ReadFieldScalar<formId>(data, dataSize);
      } else if (!std::memcmp(type, "MNAM", 4)) {
        result.worldLocationMarkerRefId =
          ReadFieldScalar<formId>(data, dataSize);
      } else if (!std::memcmp(type, "RNAM", 4)) {
        result.worldLocationRadius = ReadFieldScalar<float>(data, dataSize);
      } else if (!std::memcmp(type, "NAM0", 4)) {
        result.horseMarkerRefId = ReadFieldScalar<formId>(data, dataSize);
      } else if (!std::memcmp(type, "CNAM", 4)) {
        result.color = ReadFieldScalar<Color>(data, dataSize);
      }
    },
    compressedFieldsCache);
  return result;
}

}
