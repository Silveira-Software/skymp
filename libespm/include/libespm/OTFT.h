#pragma once
#include "RecordHeader.h"
#include <memory>
#include <vector>

#pragma pack(push, 1)

namespace espm {

class OTFT final : public RecordHeader
{
public:
  static constexpr auto kType = "OTFT";

#pragma pack(push)
#pragma pack()
  struct Data
  {
    const uint32_t* formIds = nullptr;
    uint32_t count = 0;
    std::shared_ptr<const std::vector<uint32_t>> formIdsStorage;
  };
#pragma pack(pop)
  static_assert(alignof(Data) >= alignof(decltype(Data::formIdsStorage)));

  Data GetData(CompressedFieldsCache& compressedFieldsCache) const noexcept;
};

static_assert(sizeof(OTFT) == sizeof(RecordHeader));

}

#pragma pack(pop)
