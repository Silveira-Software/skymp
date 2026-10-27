#include "LocationalDataUtils.h"
#include "NiPoint3.h"

#include "libespm/GroupUtils.h"
#include "libespm/Utils.h"
#include <cmath>
#include <cstddef>
#include <cstring>
#include <stdexcept>

namespace {
void CopyCoordinates(const espm::REFR::LocationalData* locationalData,
                     size_t offset, float (&values)[3])
{
  if (!locationalData) {
    throw std::invalid_argument("LocationalDataUtils: null locational data");
  }
  // Mapped REFR fields are packed byte streams. Do not bind NiPoint3 or float
  // references to their addresses; copy the validated POD prefix into aligned
  // scalar storage before reading it.
  std::memcpy(values,
              reinterpret_cast<const unsigned char*>(locationalData) + offset,
              sizeof(values));
}
}

NiPoint3 LocationalDataUtils::GetPos(
  const espm::REFR::LocationalData* locationalData)
{
  float position[3];
  CopyCoordinates(locationalData, offsetof(espm::REFR::LocationalData, pos),
                  position);
  return { position[0], position[1], position[2] };
}

NiPoint3 LocationalDataUtils::GetRot(
  const espm::REFR::LocationalData* locationalData)
{
  float rotation[3];
  CopyCoordinates(locationalData,
                  offsetof(espm::REFR::LocationalData, rotRadians), rotation);
  static const auto kPi = std::acos(-1.f);
  return { rotation[0] / kPi * 180.f,
           rotation[1] / kPi * 180.f,
           rotation[2] / kPi * 180.f };
}

uint32_t LocationalDataUtils::GetWorldOrCell(
  const espm::CombineBrowser& br, const espm::LookupResult& refrLookupRes)
{
  auto mapping = br.GetCombMapping(refrLookupRes.fileIdx);
  uint32_t worldOrCell = espm::utils::GetMappedId(
    espm::GetWorldOrCell(br, refrLookupRes.rec), *mapping);
  return worldOrCell;
}
