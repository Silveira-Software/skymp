#pragma once
#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <type_traits>

namespace espm {

// Plugin records and fields are byte streams and have no scalar alignment.
// Copy into a correctly aligned value before interpreting the representation.
template <class T>
T ReadUnaligned(const void* bytes) noexcept
{
  static_assert(std::is_trivially_copyable_v<T>);
  T value{};
  std::memcpy(&value, bytes, sizeof(value));
  return value;
}

template <class T>
T ReadFieldScalar(const void* bytes, std::size_t size,
                  std::size_t offset = 0)
{
  if (!bytes || offset > size || sizeof(T) > size - offset) {
    throw std::runtime_error("espm: truncated scalar field");
  }
  return ReadUnaligned<T>(static_cast<const unsigned char*>(bytes) + offset);
}

}
