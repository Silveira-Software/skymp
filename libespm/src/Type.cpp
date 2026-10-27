#include "libespm/Type.h"
#include "libespm/ReadUnaligned.h"
#include <cstring>

namespace espm {

Type::Type(const char* type_) noexcept
  : type(type_)
{
}

bool Type::operator==(const char* rhs) const noexcept
{
  return !std::memcmp(type, rhs, 4);
}

std::string Type::ToString() const noexcept
{
  return std::string{ type, 4 };
}

uint32_t Type::ToUint32() const noexcept
{
  return ReadUnaligned<uint32_t>(type);
}

}
