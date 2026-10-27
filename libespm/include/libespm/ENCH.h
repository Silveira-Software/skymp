#pragma once
#include "Effects.h"
#include "RecordHeader.h"

#pragma pack(push, 1)

namespace espm {

class ENCH final : public RecordHeader
{
public:
  static constexpr auto kType = "ENCH";

#pragma pack(push)
#pragma pack()
  struct Data
  {
    std::vector<Effects::Effect> effects;
  };
#pragma pack(pop)
  static_assert(alignof(Data) >= alignof(decltype(Data::effects)));

  Data GetData(CompressedFieldsCache& compressedFieldsCache) const noexcept;
};
static_assert(sizeof(ENCH) == sizeof(RecordHeader));

}

#pragma pack(pop)
