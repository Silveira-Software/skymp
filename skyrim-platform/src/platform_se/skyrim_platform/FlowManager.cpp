#include "FlowManager.h"
#include <Windows.h>
#include <tlhelp32.h>

namespace {
unsigned long long FileTimeValue(const FILETIME& value)
{
  ULARGE_INTEGER result{};
  result.LowPart = value.dwLowDateTime;
  result.HighPart = value.dwHighDateTime;
  return result.QuadPart;
}
bool StillOwnsChild(DWORD child, DWORD owner)
{
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot == INVALID_HANDLE_VALUE)
    return false;
  PROCESSENTRY32W entry{};
  entry.dwSize = sizeof(entry);
  bool found = false;
  if (Process32FirstW(snapshot, &entry)) {
    do {
      if (entry.th32ProcessID == child) {
        found = entry.th32ParentProcessID == owner;
        break;
      }
    } while (Process32NextW(snapshot, &entry));
  }
  CloseHandle(snapshot);
  return found;
}
}

void FlowManager::CloseChildProcesses(const std::wstring& processName)
{
  if (processName.empty())
    return;
  const DWORD owner = GetCurrentProcessId();
  FILETIME ownerCreated{}, exited{}, kernel{}, user{};
  if (!GetProcessTimes(GetCurrentProcess(), &ownerCreated, &exited, &kernel,
                       &user))
    return;
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snap == INVALID_HANDLE_VALUE)
    return;
  PROCESSENTRY32W entry{};
  entry.dwSize = sizeof(entry);
  if (!Process32FirstW(snap, &entry)) {
    CloseHandle(snap);
    return;
  }
  do {
    if (entry.th32ProcessID == owner || entry.th32ParentProcessID != owner ||
        _wcsicmp(entry.szExeFile, processName.c_str()) != 0)
      continue;
    HANDLE process =
      OpenProcess(PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                  entry.th32ProcessID);
    if (!process)
      continue;
    FILETIME created{};
    // Reject an orphan from a previous owner PID and a changed snapshot PID.
    if (GetProcessTimes(process, &created, &exited, &kernel, &user) &&
        FileTimeValue(created) >= FileTimeValue(ownerCreated) &&
        StillOwnsChild(entry.th32ProcessID, owner))
      TerminateProcess(process, 0);
    CloseHandle(process);
  } while (Process32NextW(snap, &entry));
  CloseHandle(snap);
}
