#pragma once

#include <string>

class FlowManager
{
public:
  // Only direct helpers owned by this process; never other Skyrim sessions.
  static void CloseChildProcesses(const std::wstring& processName);
};
