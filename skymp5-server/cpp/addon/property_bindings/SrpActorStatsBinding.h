#pragma once
#include "PropertyBinding.h"

class SrpActorStatsBinding : public PropertyBinding
{
public:
  std::string GetPropertyName() const override { return "srpActorStats"; }
  Napi::Value Get(Napi::Env env, ScampServer& server, uint32_t actorId) override;
  void Set(Napi::Env env, ScampServer& server, uint32_t actorId,
           Napi::Value value) override;
};
