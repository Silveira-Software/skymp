#pragma once
#include "CellOrGridPos.h"
#include "RecordHeader.h"
#include <array>
#include <memory>
#include <vector>
#include <cstddef>

#pragma pack(push, 1)

namespace espm {

class NAVM final : public RecordHeader
{
public:
  static constexpr auto kType = "NVNM";

#pragma pack(push)
#pragma pack()
  class Vertices
  {
  public:
    // Legacy pointer-only entry point retained for source compatibility.
    // Native record parsing uses the bounded overload below.
    Vertices(const void* nvnmField);
    Vertices(const void* nvnmField, std::size_t fieldSize);

    const std::array<float, 3>* begin() const noexcept;
    const std::array<float, 3>* end() const noexcept;

  private:
    std::vector<std::array<float, 3>> vertices;
  };
#pragma pack(pop)
  static_assert(alignof(Vertices) >=
                alignof(std::vector<std::array<float, 3>>));

#pragma pack(push)
#pragma pack()
  struct Data
  {
    std::unique_ptr<Vertices> vertices;
    uint32_t worldSpaceId = 0;
    CellOrGridPos cellOrGridPos = { 0 };
  };
#pragma pack(pop)
  static_assert(alignof(Data) >= alignof(decltype(Data::vertices)));

  Data GetData(CompressedFieldsCache& compressedFieldsCache) const noexcept;
};

static_assert(sizeof(NAVM) == sizeof(RecordHeader));

}

#pragma pack(pop)
