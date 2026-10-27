#include "libespm/Utils.h"
#include "libespm/ReadUnaligned.h"
#include "libespm/RecordHeaderAccess.h"
#include "libespm/ZlibUtils.h"
#include <map>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace espm::utils {

std::string ToString(GroupType type)
{
  switch (type) {
    case GroupType::TOP:
      return "TOP";
    case GroupType::WORLD_CHILDREN:
      return "WORLD_CHILDREN";
    case GroupType::INTERIOR_CELL_BLOCK:
      return "INTERIOR_CELL_BLOCK";
    case GroupType::INTERIOR_CELL_SUBBLOCK:
      return "INTERIOR_CELL_SUBBLOCK";
    case GroupType::EXTERIOR_CELL_BLOCK:
      return "EXTERIOR_CELL_BLOCK";
    case GroupType::EXTERIOR_CELL_SUBBLOCK:
      return "EXTERIOR_CELL_SUBBLOCK";
    case GroupType::CELL_CHILDREN:
      return "CELL_CHILDREN";
    case GroupType::TOPIC_CHILDREN:
      return "TOPIC_CHILDREN";
    case GroupType::CELL_PERSISTENT_CHILDREN:
      return "CELL_PERSISTENT_CHILDREN";
    case GroupType::CELL_TEMPORARY_CHILDREN:
      return "CELL_TEMPORARY_CHILDREN";
    case GroupType::CELL_VISIBLE_DISTANT_CHILDREN:
      return "CELL_VISIBLE_DISTANT_CHILDREN";
    default:
      throw std::runtime_error("unhandled case in ToString");
  }
}

bool IsItem(Type type) noexcept
{
  return type == "AMMO" || type == "ARMO" || type == "BOOK" ||
    type == "INGR" || type == "ALCH" || type == "SCRL" || type == "SLGM" ||
    type == "WEAP" || type == "MISC" ||
    type ==
    "LIGH"; // LIGH is not always an item but here we suppose it is a torch
}

uint32_t CalculateHashcode(const void* readBuffer, size_t length)
{
  return ZlibGetCRC32Checksum(readBuffer, length);
}

const std::map<std::string, uint32_t> kCorrectHashcode{
  { "Skyrim.esm", 0xaf75991dUL },
  { "Update.esm", 0x17ab5e20UL },
  { "Dawnguard.esm", 0xcc81e5d8UL },
  { "HearthFires.esm", 0xbad9393aUL },
  { "Dragonborn.esm", 0xeb10e82UL }
};

uint32_t GetCorrectHashcode(const std::string& fileName)
{
  auto iter = kCorrectHashcode.find(fileName);
  return iter == kCorrectHashcode.end() ? 0 : iter->second;
}

uint32_t GetMappedId(uint32_t id, const IdMapping& mapping) noexcept
{
  const uint32_t shortId = id % 0x01000000;
  const uint8_t index = id / 0x01000000;
  return shortId + (static_cast<uint32_t>(mapping[index]) << 24);
}

std::wstring ReadWstring(const uint8_t* ptr)
{
  const uint16_t scriptNameSize = ReadUnaligned<uint16_t>(ptr);
  const wchar_t* scriptName = reinterpret_cast<const wchar_t*>(ptr + 2);
  return std::wstring(scriptName, scriptNameSize / 2);
}

Property::Type GetElementType(Property::Type arrayType)
{
  return static_cast<Property::Type>(
    static_cast<std::underlying_type_t<Property::Type>>(arrayType) - 10);
}

namespace {
void RequireScriptBytes(const uint8_t* p, const uint8_t* end, size_t count)
{
  if (end && (p > end || count > static_cast<size_t>(end - p))) {
    throw std::runtime_error("espm: truncated VMAD property");
  }
}
}

const uint8_t* ReadPropertyValue(const uint8_t* p, Property* prop,
                                 uint16_t objFormat)
{
  return ReadPropertyValue(p, prop, objFormat, nullptr);
}

const uint8_t* ReadPropertyValue(const uint8_t* p, Property* prop,
                                 uint16_t objFormat, const uint8_t* end)
{
  const auto t = prop->type;
  switch (t) {
    case Property::Type::Object:
      RequireScriptBytes(p, end, 8);
      if (objFormat == 1)
        prop->value.formId = ReadUnaligned<uint32_t>(p);
      else if (objFormat == 2)
        prop->value.formId = ReadUnaligned<uint32_t>(p + 4);
      else
        throw std::runtime_error("Unknown objFormat (" +
                                 std::to_string(objFormat) + ")");
      return p + 8;
    case Property::Type::Int:
      RequireScriptBytes(p, end, 4);
      prop->value.integer = ReadUnaligned<int32_t>(p);
      return p + 4;
    case Property::Type::Float:
      RequireScriptBytes(p, end, 4);
      prop->value.floatingPoint = ReadUnaligned<float>(p);
      return p + 4;
    case Property::Type::Bool:
      RequireScriptBytes(p, end, 1);
      prop->value.boolean = ReadUnaligned<int8_t>(p);
      return p + 1;
    case Property::Type::String: {
      RequireScriptBytes(p, end, 2);
      const auto length = ReadUnaligned<uint16_t>(p);
      p += 2;
      RequireScriptBytes(p, end, length);
      prop->value.str = { reinterpret_cast<const char*>(p), length };
      return p + length;
    }
    case Property::Type::ObjectArray:
    case Property::Type::IntArray:
    case Property::Type::FloatArray:
    case Property::Type::BoolArray:
    case Property::Type::StringArray: {
      RequireScriptBytes(p, end, 4);
      const auto arrayLength = ReadUnaligned<uint32_t>(p);
      p += 4;
      for (uint32_t i = 0; i < arrayLength; ++i) {
        Property element;
        element.type = GetElementType(t);
        p = ReadPropertyValue(p, &element, objFormat, end);
        prop->array.push_back(element.value);
      }
      return p;
    }
    default:
      throw std::runtime_error(
        "Script properties with type " +
        std::to_string(
          static_cast<std::underlying_type_t<Property::Type>>(t)) +
        " are not yet supported");
  }
}

void FillScriptArray(const uint8_t* p, std::vector<Script>& out,
                     uint16_t objFormat)
{
  FillScriptArray(p, out, objFormat, nullptr);
}

void FillScriptArray(const uint8_t* p, std::vector<Script>& out,
                     uint16_t objFormat, const uint8_t* end)
{
  for (size_t i = 0; i < out.size(); ++i) {
    RequireScriptBytes(p, end, 2);
    const auto length = ReadUnaligned<uint16_t>(p);
    p += 2;
    RequireScriptBytes(p, end, length);
    out[i].scriptName = std::string(reinterpret_cast<const char*>(p), length);
    p += length;
    RequireScriptBytes(p, end, 3);
    out[i].status = *p;
    ++p;
    const auto numProperties = ReadUnaligned<uint16_t>(p);
    p += 2;
    for (uint16_t j = 0; j < numProperties; ++j) {
      RequireScriptBytes(p, end, 2);
      const auto propertyLength = ReadUnaligned<uint16_t>(p);
      p += 2;
      RequireScriptBytes(p, end, propertyLength);
      Property prop;
      prop.name = std::string(reinterpret_cast<const char*>(p), propertyLength);
      p += propertyLength;
      RequireScriptBytes(p, end, 2);
      prop.type = static_cast<Property::Type>(*p);
      ++p;
      prop.status = *p;
      ++p;
      p = ReadPropertyValue(p, &prop, objFormat, end);
      out[i].properties.insert(prop);
    }
  }
}

uint64_t MakeUInt64(uint32_t high, uint32_t low)
{
  return (static_cast<uint64_t>(high) << 32) | static_cast<uint64_t>(low);
}

std::vector<CONT::ContainerObject> GetContainerObjects(
  const RecordHeader* rec, CompressedFieldsCache& compressedFieldsCache)
{
  std::vector<CONT::ContainerObject> objects;
  RecordHeaderAccess::IterateFields(
    rec,
    [&](const char* type, uint32_t dataSize, const char* data) {
      if (!std::memcmp(type, "CNTO", 4)) {
        objects.push_back(
          ReadFieldScalar<CONT::ContainerObject>(data, dataSize));
      } else if (!std::memcmp(type, "COED", 4)) {
        // Not supported
      }
    },
    compressedFieldsCache);
  return objects;
}
}
