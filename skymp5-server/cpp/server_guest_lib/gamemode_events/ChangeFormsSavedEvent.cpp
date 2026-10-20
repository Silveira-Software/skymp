#include "ChangeFormsSavedEvent.h"

ChangeFormsSavedEvent::ChangeFormsSavedEvent(uint64_t batchId_)
  : batchId(batchId_)
{
}

const char* ChangeFormsSavedEvent::GetName() const
{
  return "onChangeFormsSaved";
}

std::string ChangeFormsSavedEvent::GetArgumentsJsonArray() const
{
  return "[" + std::to_string(batchId) + "]";
}
