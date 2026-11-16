#pragma once
#include "Networking.h"

class Bot
{
public:
  Bot(std::shared_ptr<Networking::IClient> cl_)
    : cl(cl_)
  {
  }

  void Destroy() { cl.reset(); }

  void Send(std::string packet)
  {
    if (cl)
      cl->Send(reinterpret_cast<Networking::PacketData>(packet.data()),
               packet.size(), true);
  }

  void Tick()
  {
    if (cl)
      cl->Tick([](void* state, Networking::PacketType type, Networking::PacketData data,
                  size_t length, const char*) {
        auto& packets = static_cast<Bot*>(state)->received;
        if (type == Networking::PacketType::Message && data && length &&
            length <= 1024*1024 && packets.size() < 256)
          packets.emplace_back(reinterpret_cast<const char*>(data), length);
      }, this);
  }

  std::vector<std::string> Receive() { Tick(); auto result = std::move(received); received.clear(); return result; }

private:
  std::shared_ptr<Networking::IClient> cl;
  std::vector<std::string> received;
};
