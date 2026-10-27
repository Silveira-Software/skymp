#pragma once
#include "RecordHeader.h"

#pragma pack(push, 1)

namespace espm {

class FLST final : public RecordHeader
{
public:
  static constexpr auto kType = "FLST";

#pragma pack(push)
#pragma pack()
  struct Data
  {
    std::vector<uint32_t> formIds;
  };
#pragma pack(pop)
  static_assert(alignof(Data) >= alignof(decltype(Data::formIds)));

  Data GetData(CompressedFieldsCache& compressedFieldsCache) const noexcept;
};

static_assert(sizeof(FLST) == sizeof(RecordHeader));

}

#pragma pack(pop)
