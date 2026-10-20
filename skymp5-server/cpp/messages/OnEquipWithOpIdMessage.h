#pragma once
#include "MessageBase.h"
#include "MsgType.h"
#include <string>
#include <type_traits>

// SRP experimental. Same as OnEquipMessage plus a client-generated operation
// id that stays the same when the client resends the same intent. Old
// clients keep sending OnEquipMessage, which behaves exactly as before.
struct OnEquipWithOpIdMessage : public MessageBase<OnEquipWithOpIdMessage>
{
  static constexpr auto kMsgType =
    std::integral_constant<char,
                           static_cast<char>(MsgType::OnEquipWithOpId)>{};

  template <class Archive>
  void Serialize(Archive& archive)
  {
    archive.Serialize("t", kMsgType)
      .Serialize("baseId", baseId)
      .Serialize("opId", opId);
  }

  uint32_t baseId = 0;
  std::string opId;
};
