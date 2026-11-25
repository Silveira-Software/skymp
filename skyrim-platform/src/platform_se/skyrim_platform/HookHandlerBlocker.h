#pragma once
#include <atomic>

// The blocker covers execution, not the lifetime of a deferred queue item.
class HookHandlerBlocker
{
public:
  explicit HookHandlerBlocker(std::atomic<int>& counter_)
    : counter(counter_)
  {
    ++counter;
  }

  ~HookHandlerBlocker() noexcept { --counter; }
  HookHandlerBlocker(const HookHandlerBlocker&) = delete;
  HookHandlerBlocker& operator=(const HookHandlerBlocker&) = delete;

private:
  std::atomic<int>& counter;
};
