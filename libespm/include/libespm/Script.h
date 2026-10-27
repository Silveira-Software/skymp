#pragma once
#include "Property.h"
#include <set>


namespace espm {
struct Script
{
  std::string scriptName;
  uint8_t status = 0;
  std::set<Property> properties;
};

}

