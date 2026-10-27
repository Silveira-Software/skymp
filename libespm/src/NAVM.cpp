#include "libespm/NAVM.h"
#include "libespm/ReadUnaligned.h"
#include "libespm/RecordHeaderAccess.h"
#include <cstring>
#include <limits>

namespace espm {

NAVM::Vertices::Vertices(const void* nvnmField)
  : Vertices(nvnmField, std::numeric_limits<std::size_t>::max())
{
}

NAVM::Vertices::Vertices(const void* nvnmField, std::size_t fieldSize)
{
  const auto count = ReadFieldScalar<int32_t>(nvnmField, fieldSize, 16);
  if (count < 0 || static_cast<uint32_t>(count) >
        (fieldSize - 20) / sizeof(std::array<float, 3>)) {
    throw std::runtime_error("espm: invalid navmesh vertex count");
  }
  vertices.resize(static_cast<std::size_t>(count));
  if (!vertices.empty()) {
    std::memcpy(vertices.data(),
                static_cast<const unsigned char*>(nvnmField) + 20,
                vertices.size() * sizeof(vertices[0]));
  }
}

const std::array<float, 3>* NAVM::Vertices::begin() const noexcept
{
  return vertices.data();
}

const std::array<float, 3>* NAVM::Vertices::end() const noexcept
{
  return vertices.empty() ? vertices.data() : vertices.data() + vertices.size();
}

NAVM::Data NAVM::GetData(
  CompressedFieldsCache& compressedFieldsCache) const noexcept
{
  Data result;
  RecordHeaderAccess::IterateFields(
    this,
    [&](const char* type, uint32_t dataSize, const char* data) {
      if (!std::memcmp(type, "NVNM", 4)) {
        const auto count = ReadFieldScalar<int32_t>(data, dataSize, 16);
        if (count < 0 || static_cast<uint32_t>(count) >
              (dataSize - 20) / sizeof(std::array<float, 3>)) {
          throw std::runtime_error("espm: invalid navmesh vertex count");
        }
        result.worldSpaceId = ReadFieldScalar<uint32_t>(data, dataSize, 8);
        result.cellOrGridPos = ReadFieldScalar<CellOrGridPos>(data, dataSize, 12);
        result.vertices.reset(new Vertices(data, dataSize));
      }
    },
    compressedFieldsCache);
  return result;
}

}
