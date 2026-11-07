// Standalone regression, compiled with the production FlowManager.cpp.
#include "../src/platform_se/skyrim_platform/FlowManager.h"
#include <Windows.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
namespace {
struct Process
{
  HANDLE handle;
  DWORD pid;
};
std::vector<HANDLE> owned;
void Require(bool value, const char* why)
{
  if (!value)
    throw std::runtime_error(why);
}
Process Spawn(const std::wstring& path, const std::wstring& args,
              HANDLE output = nullptr)
{
  std::wstring command = L"\"" + path + L"\" " + args;
  STARTUPINFOW info{};
  info.cb = sizeof(info);
  if (output) {
    info.dwFlags = STARTF_USESTDHANDLES;
    info.hStdOutput = output;
    info.hStdError = output;
    info.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  }
  PROCESS_INFORMATION p{};
  Require(CreateProcessW(path.c_str(), command.data(), nullptr, nullptr,
                         output != nullptr, CREATE_NO_WINDOW, nullptr, nullptr,
                         &info, &p),
          "spawn failed");
  CloseHandle(p.hThread);
  owned.push_back(p.hProcess);
  return { p.hProcess, p.dwProcessId };
}
bool Alive(HANDLE p)
{
  return WaitForSingleObject(p, 0) == WAIT_TIMEOUT;
}
void Cleanup()
{
  for (auto p : owned) {
    if (Alive(p))
      TerminateProcess(p, 0);
    WaitForSingleObject(p, 5000);
    CloseHandle(p);
  }
  owned.clear();
}
}
int wmain(int argc, wchar_t** argv)
{
  if (argc > 1 && std::wstring(argv[1]) == L"--hold") {
    Sleep(INFINITE);
    return 0;
  }
  if (argc > 2 && std::wstring(argv[1]) == L"--owner") {
    auto child = Spawn(argv[2], L"--hold");
    auto text = std::to_string(child.pid) + "\n";
    DWORD n = 0;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), text.data(),
              static_cast<DWORD>(text.size()), &n, nullptr);
    Sleep(INFINITE);
    return 0;
  }
  try {
    wchar_t self[32768]{};
    Require(GetModuleFileNameW(nullptr, self, 32768) > 0, "self path");
    auto dir = std::filesystem::path(self).parent_path();
    auto cef = dir / L"SkyrimPlatformCEF.exe.hidden",
         game = dir / L"SkyrimSE.exe";
    std::filesystem::copy_file(
      self, cef, std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file(
      self, game, std::filesystem::copy_options::overwrite_existing);
    SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
    HANDLE read = nullptr, write = nullptr;
    Require(CreatePipe(&read, &write, &sa, 0), "pipe");
    SetHandleInformation(read, HANDLE_FLAG_INHERIT, 0);
    auto other =
      Spawn(game.wstring(), L"--owner \"" + cef.wstring() + L"\"", write);
    CloseHandle(write);
    std::string id;
    char c = 0;
    DWORD n = 0;
    while (ReadFile(read, &c, 1, &n, nullptr) && n && c != '\n')
      id += c;
    CloseHandle(read);
    Require(!id.empty(), "helper handshake");
    auto otherCef =
      OpenProcess(SYNCHRONIZE | PROCESS_TERMINATE, FALSE, std::stoul(id));
    Require(otherCef != nullptr, "other helper handle");
    owned.push_back(otherCef);
    auto own = Spawn(cef.wstring(), L"--hold"),
         unrelated = Spawn(game.wstring(), L"--hold");
    FlowManager::CloseChildProcesses(L"");
    Require(Alive(own.handle) && Alive(otherCef), "empty selector");
    FlowManager::CloseChildProcesses(L"unknown.exe");
    Require(Alive(own.handle) && Alive(otherCef), "unknown selector");
    FlowManager::CloseChildProcesses(L"skyrimplatformcef.exe.hidden");
    Require(WaitForSingleObject(own.handle, 5000) == WAIT_OBJECT_0,
            "own helper not closed");
    Require(Alive(other.handle), "other game killed");
    Require(Alive(otherCef), "other helper killed");
    Require(Alive(unrelated.handle), "unrelated game killed");
    FlowManager::CloseChildProcesses(L"SkyrimPlatformCEF.exe.hidden");
    Require(Alive(other.handle) && Alive(otherCef),
            "repeat cleanup crossed sessions");
    Cleanup();
    std::filesystem::remove(cef);
    std::filesystem::remove(game);
    std::cout << "{\"status\":\"PASS\",\"checks\":7,"
                 "\"actualWindowsProcesses\":true,\"realSkyrim\":false}"
              << std::endl;
    return 0;
  } catch (const std::exception& e) {
    Cleanup();
    std::cerr << e.what() << std::endl;
    return 1;
  }
}
