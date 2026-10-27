#include "libespm/OTFT.h"
#include "libespm/RecordHeaderAccess.h"
#include <cstring>
#include <stdexcept>

namespace espm {

OTFT::Data OTFT::GetData(
  CompressedFieldsCache& compressedFieldsCache) const noexcept
{
  Data result;
  RecordHeaderAccess::IterateFields(
    this,
    [&](const char* type, uint32_t dataSize, const char* data) {
      if (!std::memcmp(type, "INAM", 4)) {
        if (dataSize % sizeof(uint32_t) != 0) {
          throw std::runtime_error("espm: truncated outfit form array");
        }
        auto ids = std::make_shared<std::vector<uint32_t>>(
          dataSize / sizeof(uint32_t));
        if (!ids->empty()) {
          std::memcpy(ids->data(), data, dataSize);
        }
        result.count = static_cast<uint32_t>(ids->size());
        result.formIdsStorage = ids;
        result.formIds = ids->data();
      }
    },
    compressedFieldsCache);
  return result;
}

}
