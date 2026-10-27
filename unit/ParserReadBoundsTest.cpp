#include <catch2/catch_all.hpp>
#include "libespm/Browser.h"
#include "libespm/Utils.h"
#include "libespm/CompressedFieldsCache.h"
#include "libespm/REFR.h"
#include "LocationalDataUtils.h"
#include "NiPoint3.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace {
void Word(char* p, uint32_t value) { std::memcpy(p, &value, sizeof(value)); }
std::vector<char> RefrField(const char* type, uint16_t payloadSize)
{
  // Offset the entire record deliberately, keeping its raw header and field
  // bytes unaligned. The record layout is 24 bytes, then a 6-byte field header.
  std::vector<char> bytes(1 + 24 + 6 + payloadSize, '\0');
  char* start = bytes.data() + 1;
  std::memcpy(start, "REFR", 4);
  Word(start + 4, 6U + payloadSize);
  Word(start + 12, 0x01020304);
  std::memcpy(start + 24, type, 4);
  std::memcpy(start + 28, &payloadSize, sizeof(payloadSize));
  return bytes;
}
}

TEST_CASE("Browser accepts unaligned valid record and group headers", "[ParserBounds]")
{
  std::array<char, 49> bytes{};
  char* start = bytes.data() + 1;
  std::memcpy(start, "GMST", 4);
  Word(start + 4, 0);
  Word(start + 12, 0x01020304);
  std::memcpy(start + 24, "GRUP", 4);
  Word(start + 28, 24);
  espm::Browser browser(start, 48);
  const auto* record = browser.LookupById(0x01020304);
  REQUIRE(record);
  REQUIRE(record->GetId() == 0x01020304);
}

TEST_CASE("Browser rejects truncated and oversized headers before access", "[ParserBounds]")
{
  std::array<char, 64> bytes{};
  for (size_t length = 1; length < 24; ++length) {
    REQUIRE_THROWS_AS(espm::Browser(bytes.data() + 1, length), std::runtime_error);
  }
  char* start = bytes.data() + 1;
  std::memcpy(start, "GRUP", 4);
  for (uint32_t size : {0U, 23U, 25U, 0xffffffffU}) {
    Word(start + 4, size);
    REQUIRE_THROWS_AS(espm::Browser(start, 24), std::runtime_error);
  }
  std::memcpy(start, "GMST", 4);
  Word(start + 4, 1);
  REQUIRE_THROWS_AS(espm::Browser(start, 24), std::runtime_error);
}

TEST_CASE("Browser rejects child groups beyond their parent boundary", "[ParserBounds]")
{
  std::array<char, 64> bytes{};
  char* start = bytes.data() + 1;
  std::memcpy(start, "GRUP", 4);
  Word(start + 4, 48);
  std::memcpy(start + 24, "GRUP", 4);
  Word(start + 28, 36);
  REQUIRE_THROWS_AS(espm::Browser(start, 60), std::runtime_error);
}

TEST_CASE("Locational utilities copy deliberately unaligned position and rotation", "[ParserBounds]")
{
  static_assert(std::is_same_v<decltype(LocationalDataUtils::GetPos(nullptr)), NiPoint3>);
  constexpr float position[3] = { 123.5f, -2048.25f, 0.5f };
  const float pi = std::acos(-1.f);
  const float rotation[3] = { pi / 2, -pi / 4, pi };
  alignas(float) std::array<unsigned char, 24 + alignof(float)> bytes{};
  for (size_t offset = 1; offset < alignof(float); ++offset) {
    unsigned char* start = bytes.data() + offset;
    std::memcpy(start, position, sizeof(position));
    std::memcpy(start + sizeof(position), rotation, sizeof(rotation));
    const auto* loc = reinterpret_cast<const espm::REFR::LocationalData*>(start);
    REQUIRE(reinterpret_cast<uintptr_t>(start) % alignof(float) != 0);
    const auto pos = LocationalDataUtils::GetPos(loc);
    const auto rot = LocationalDataUtils::GetRot(loc);
    REQUIRE(pos.x == position[0]);
    REQUIRE(pos.y == position[1]);
    REQUIRE(pos.z == position[2]);
    REQUIRE(std::abs(rot.x - 90.f) < 0.001f);
    REQUIRE(std::abs(rot.y + 45.f) < 0.001f);
    REQUIRE(std::abs(rot.z - 180.f) < 0.001f);
    // A returned position owns its values and cannot alias mapped bytes.
    std::memset(start, 0, 24);
    REQUIRE(pos.x == position[0]);
    REQUIRE(pos.y == position[1]);
    REQUIRE(pos.z == position[2]);
  }
}

TEST_CASE("Locational utilities reject null mapped data", "[ParserBounds]")
{
  REQUIRE_THROWS_AS(LocationalDataUtils::GetPos(nullptr), std::invalid_argument);
  REQUIRE_THROWS_AS(LocationalDataUtils::GetRot(nullptr), std::invalid_argument);
}

TEST_CASE("REFR rejects short DATA and XTEL before exposing mapped pointers", "[ParserBounds]")
{
  for (const auto& field : { std::pair<const char*, uint16_t>{"DATA", 24},
                            std::pair<const char*, uint16_t>{"XTEL", 28} }) {
    for (uint16_t size = 0; size < field.second; ++size) {
      const auto bytes = RefrField(field.first, size);
      const auto* record = reinterpret_cast<const espm::REFR*>(bytes.data() + 1 + 8);
      espm::CompressedFieldsCache cache;
      REQUIRE_THROWS_AS(record->GetData(cache), std::runtime_error);
      // Browser's REFR coordinate indexing must receive the same propagated
      // rejection instead of reading a short payload or terminating noexcept.
      REQUIRE_THROWS_AS(espm::Browser(bytes.data() + 1, bytes.size() - 1), std::runtime_error);
    }
  }
}

TEST_CASE("REFR accepts complete DATA and XTEL mapped prefixes with trailing bytes", "[ParserBounds]")
{
  for (const auto& field : { std::pair<const char*, uint16_t>{"DATA", 24},
                            std::pair<const char*, uint16_t>{"XTEL", 28} }) {
    for (uint16_t extra : {uint16_t{0}, uint16_t{4}}) {
      const auto bytes = RefrField(field.first, field.second + extra);
      const auto* record = reinterpret_cast<const espm::REFR*>(bytes.data() + 1 + 8);
      espm::CompressedFieldsCache cache;
      const auto data = record->GetData(cache);
      const void* expected = bytes.data() + 1 + 24 + 6;
      if (std::memcmp(field.first, "DATA", 4) == 0) {
        REQUIRE(static_cast<const void*>(data.loc) == expected);
        REQUIRE(LocationalDataUtils::GetPos(data.loc) == NiPoint3(0, 0, 0));
        REQUIRE(LocationalDataUtils::GetRot(data.loc) == NiPoint3(0, 0, 0));
      } else {
        REQUIRE(static_cast<const void*>(data.teleport) == expected);
      }
    }
  }
}

TEST_CASE("Form ID remapping preserves high plugin bytes without signed overflow", "[ParserBounds][FormIdMapping]")
{
  espm::IdMapping mapping{};
  for (size_t i = 0; i < mapping.size(); ++i)
    mapping[i] = static_cast<uint8_t>(i);
  REQUIRE(espm::utils::GetMappedId(0xff123456U, mapping) == 0xff123456U);
  mapping[0xff] = 0x80;
  REQUIRE(espm::utils::GetMappedId(0xff123456U, mapping) == 0x80123456U);
  mapping[0xff] = 0;
  REQUIRE(espm::utils::GetMappedId(0xff123456U, mapping) == 0x00123456U);
}
