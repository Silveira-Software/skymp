#pragma once
#include "RecordHeader.h"

#pragma pack(push, 1)

namespace espm {

class CONT final : public RecordHeader
{
public:
  static constexpr auto kType = "CONT";

  struct ContainerObject
  {
    uint32_t formId = 0;
    uint32_t count = 0;
  };

#pragma pack(push)
#pragma pack()
  struct Data
  {
    const char* editorId = "";
    const char* fullName = "";
    std::vector<ContainerObject> objects;
  };
#pragma pack(pop)
  static_assert(alignof(Data) >= alignof(decltype(Data::objects)));

  Data GetData(CompressedFieldsCache& compressedFieldsCache) const noexcept;
};
static_assert(sizeof(CONT) == sizeof(RecordHeader));

}

#pragma pack(pop)
