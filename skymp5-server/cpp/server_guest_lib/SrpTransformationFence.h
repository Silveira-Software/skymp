#pragma once
#include "MpActor.h"

// The private world outbox owns this fence. A client cannot set or release it.
// Unknown/corrupt versions stay fenced; ordinary actors have no such field.
inline bool SrpTransformationFenced(const MpActor& actor)
{
  const auto& fields = actor.GetDynamicFields().GetAsJson();
  const auto it = fields.find("private.srpTransformationV1");
  if (it == fields.end()) return false;
  if (!it->is_object() || !it->contains("schemaVersion") ||
      !it->at("schemaVersion").is_number_integer() || it->at("schemaVersion") != 1) return true;
  if (!it->contains("fenced") || !it->at("fenced").is_boolean()) return true;
  return it->at("fenced").get<bool>();
}
