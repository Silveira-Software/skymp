#pragma once
#include "Effects.h"
#include "RecordHeader.h"

#pragma pack(push, 1)

namespace espm {

class INGR final : public RecordHeader
{
public:
  static constexpr auto kType = "INGR";

  struct ItemData
  {
    uint32_t value;
    float weight;
  };

#pragma pack(push)
#pragma pack()
  struct Data
  {
    ItemData itemData;
    std::vector<Effects::Effect> effects;
  };
#pragma pack(pop)
  static_assert(alignof(Data) >= alignof(decltype(Data::effects)));

  Data GetData(CompressedFieldsCache& compressedFieldsCache) const noexcept;
};

static_assert(sizeof(INGR) == sizeof(RecordHeader));

}

#pragma pack(pop)
