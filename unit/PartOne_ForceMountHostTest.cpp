#include "TestUtils.hpp"

#include "HostStartMessage.h"
#include "HostStopMessage.h"
#include <spdlog/sinks/ostream_sink.h>
#include <spdlog/spdlog.h>
#include <sstream>

extern espm::Loader& GetEspmLoader();

using Catch::Matchers::ContainsSubstring;

namespace {
// Must match ActionListener.cpp (bit 33 of HostMessage::remoteId).
constexpr uint64_t kForceMountHostFlag = 0x200000000ULL;

constexpr uint32_t kOwnerActor = 0xff000001;
constexpr uint32_t kSpectatorActor = 0xff000002;
constexpr uint32_t kHorse = 0xff000010;
constexpr Networking::UserId kOwnerUser = 0;
constexpr Networking::UserId kSpectatorUser = 1;

class ForceMountHostFixture
{
  // Declared first: the logger sink writes here until partOne is destroyed.
  std::ostringstream logStream;

public:
  ForceMountHostFixture()
  {
    auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(logStream);
    auto logger = std::make_shared<spdlog::logger>("force-mount-host", sink);
    logger->set_pattern("%v");
    logger->set_level(spdlog::level::info);
    partOne.AttachLogger(logger);

    partOne.AttachEspm(&GetEspmLoader());
    partOne.worldState.bannedEspmCharacterRaceIds.clear();
    for (Networking::UserId user : { kOwnerUser, kSpectatorUser }) {
      const uint32_t actorId =
        user == kOwnerUser ? kOwnerActor : kSpectatorActor;
      DoConnect(partOne, user);
      partOne.CreateActor(actorId, { 1.f, 2.f, 3.f }, 180.f, 0x3c);
      partOne.SetUserActor(user, actorId);
    }
    partOne.CreateActor(kHorse, { 1.f, 2.f, 3.f }, 180.f, 0x3c);
    horse = std::dynamic_pointer_cast<MpActor>(
      partOne.worldState.LookupFormById(kHorse));
    REQUIRE(horse);
  }

  // Same dump the gamemode produces with mp.set(horse, "srpMountOwner", id).
  void SetMountOwnerDump(const std::string& dump)
  {
    horse->SetPropertyValueDump("srpMountOwner", dump, false, false);
  }

  void SetMountOwner(uint32_t actorId)
  {
    SetMountOwnerDump(std::to_string(actorId));
  }

  void RequestHost(Networking::UserId user, bool force)
  {
    const uint64_t remoteId =
      static_cast<uint64_t>(kHorse) + (force ? kForceMountHostFlag : 0);
    DoMessage(partOne, user,
              { { "t", MsgType::Host }, { "remoteId", remoteId } });
  }

  void SetHorseMovementAge(std::chrono::seconds age)
  {
    partOne.worldState.lastMovUpdateByIdx.at(horse->GetIdx()) =
      std::chrono::system_clock::now() - age;
  }

  // Another player hosts the horse and keeps streaming its movement.
  void SpectatorHostsHorse()
  {
    RequestHost(kSpectatorUser, false);
    REQUIRE(partOne.worldState.hosters.at(kHorse) == kSpectatorActor);
    SetHorseMovementAge(std::chrono::seconds(0));
    ClearOutput();
  }

  void ClearOutput()
  {
    partOne.Messages().clear();
    logStream.str("");
    logStream.clear();
  }

  template <class Message>
  size_t CountSent(Networking::UserId user)
  {
    size_t n = 0;
    for (auto& m : partOne.Messages()) {
      auto typed = dynamic_cast<Message*>(m.message.get());
      if (typed && m.userId == user && typed->target == kHorse) {
        ++n;
      }
    }
    return n;
  }

  std::string Log() const { return logStream.str(); }

  PartOne partOne;
  std::shared_ptr<MpActor> horse;
};
}

TEST_CASE("Mount owner with force flag takes the host immediately",
          "[PartOne][HostBounds][ForceMountHost]")
{
  ForceMountHostFixture f;
  f.SetMountOwner(kOwnerActor);
  f.SpectatorHostsHorse();

  f.RequestHost(kOwnerUser, true);

  REQUIRE(f.partOne.worldState.hosters.at(kHorse) == kOwnerActor);
  REQUIRE(f.CountSent<HostStartMessage>(kOwnerUser) == 1);
  REQUIRE(f.CountSent<HostStopMessage>(kSpectatorUser) == 1);
  REQUIRE(f.CountSent<HostStartMessage>(kSpectatorUser) == 0);
  REQUIRE_THAT(f.Log(),
               ContainsSubstring(
                 "Hoster changed from ff000002 to ff000001 (mounted horse)"));

  // The previous hoster can no longer stream the horse.
  DoUpdateMovement(f.partOne, kHorse, kSpectatorUser);
  REQUIRE(f.partOne.worldState.hosters.at(kHorse) == kOwnerActor);
}

TEST_CASE("Non-owner with force flag is rejected",
          "[PartOne][HostBounds][ForceMountHost]")
{
  ForceMountHostFixture f;
  f.SetMountOwner(kOwnerActor);

  SECTION("horse hosted by its owner")
  {
    f.RequestHost(kOwnerUser, true);
    REQUIRE(f.partOne.worldState.hosters.at(kHorse) == kOwnerActor);
    f.SetHorseMovementAge(std::chrono::seconds(0));
    f.ClearOutput();

    f.RequestHost(kSpectatorUser, true);

    REQUIRE(f.partOne.worldState.hosters.at(kHorse) == kOwnerActor);
  }

  SECTION("horse without any hoster")
  {
    // Without the flag this request would be accepted (hoster == 0). The
    // flag must not open a second path to the host.
    f.RequestHost(kSpectatorUser, true);

    REQUIRE(f.partOne.worldState.hosters.count(kHorse) == 0);
  }

  REQUIRE(f.CountSent<HostStartMessage>(kSpectatorUser) == 0);
  REQUIRE(f.CountSent<HostStopMessage>(kOwnerUser) == 0);
  REQUIRE_THAT(f.Log(),
               ContainsSubstring("Rejected forced mount host: "
                                 "requester=ff000002, horse=ff000010, "
                                 "owner=ff000001"));
  REQUIRE_THAT(f.Log(), !ContainsSubstring("Hoster changed"));
}

TEST_CASE("Host request without force flag keeps the 2 second rule",
          "[PartOne][HostBounds][ForceMountHost]")
{
  ForceMountHostFixture f;
  f.SetMountOwner(kOwnerActor);
  f.SpectatorHostsHorse();

  // Fresh movement from the current hoster: even the owner must wait.
  f.RequestHost(kOwnerUser, false);

  REQUIRE(f.partOne.worldState.hosters.at(kHorse) == kSpectatorActor);
  REQUIRE(f.CountSent<HostStartMessage>(kOwnerUser) == 0);
  REQUIRE(f.CountSent<HostStopMessage>(kSpectatorUser) == 0);
  REQUIRE_THAT(f.Log(), !ContainsSubstring("Hoster changed"));

  // Movement older than 2 s: regular takeover, without the mount suffix.
  f.SetHorseMovementAge(std::chrono::seconds(3));
  f.RequestHost(kOwnerUser, false);

  REQUIRE(f.partOne.worldState.hosters.at(kHorse) == kOwnerActor);
  REQUIRE(f.CountSent<HostStartMessage>(kOwnerUser) == 1);
  REQUIRE(f.CountSent<HostStopMessage>(kSpectatorUser) == 1);
  REQUIRE_THAT(f.Log(),
               ContainsSubstring("Hoster changed from ff000002 to ff000001"));
  REQUIRE_THAT(f.Log(), !ContainsSubstring("(mounted horse)"));
}

TEST_CASE("Force flag on a horse without a valid srpMountOwner is rejected",
          "[PartOne][HostBounds][ForceMountHost]")
{
  ForceMountHostFixture f;

  std::string ownerDump;
  SECTION("field never written")
  {
  }
  SECTION("null")
  {
    ownerDump = "null";
  }
  SECTION("zero")
  {
    ownerDump = "0";
  }
  SECTION("negative")
  {
    ownerDump = "-16777215";
  }
  SECTION("wider than a form id")
  {
    ownerDump =
      std::to_string(static_cast<uint64_t>(kOwnerActor) + 0x100000000ULL);
  }
  SECTION("string")
  {
    ownerDump = "\"" + std::to_string(kOwnerActor) + "\"";
  }
  SECTION("fractional number")
  {
    ownerDump = std::to_string(kOwnerActor) + ".5";
  }
  SECTION("object")
  {
    ownerDump = "{\"owner\":" + std::to_string(kOwnerActor) + "}";
  }
  if (!ownerDump.empty()) {
    f.SetMountOwnerDump(ownerDump);
  }
  f.SpectatorHostsHorse();

  f.RequestHost(kOwnerUser, true);

  REQUIRE(f.partOne.worldState.hosters.at(kHorse) == kSpectatorActor);
  REQUIRE(f.CountSent<HostStartMessage>(kOwnerUser) == 0);
  REQUIRE(f.CountSent<HostStopMessage>(kSpectatorUser) == 0);
  REQUIRE_THAT(f.Log(),
               ContainsSubstring("Rejected forced mount host: "
                                 "requester=ff000001, horse=ff000010, "
                                 "owner=0"));
}

TEST_CASE("Client UpdateProperty cannot forge srpMountOwner",
          "[PartOne][HostBounds][ForceMountHost]")
{
  ForceMountHostFixture f;
  f.SetMountOwner(kOwnerActor);
  f.RequestHost(kOwnerUser, true);
  REQUIRE(f.partOne.worldState.hosters.at(kHorse) == kOwnerActor);
  f.SetHorseMovementAge(std::chrono::seconds(0));
  f.ClearOutput();
  const std::string ownerDumpBefore =
    f.horse->GetDynamicFields().GetValueDump("srpMountOwner");

  REQUIRE_NOTHROW(
    DoMessage(f.partOne, kSpectatorUser,
              { { "t", MsgType::UpdateProperty },
                { "idx", f.horse->GetIdx() },
                { "propName", "srpMountOwner" },
                { "refrId", kHorse },
                { "dataDump", std::to_string(kSpectatorActor) } }));

  REQUIRE(f.horse->GetDynamicFields().GetValueDump("srpMountOwner") ==
          ownerDumpBefore);

  f.RequestHost(kSpectatorUser, true);

  REQUIRE(f.partOne.worldState.hosters.at(kHorse) == kOwnerActor);
  REQUIRE(f.CountSent<HostStartMessage>(kSpectatorUser) == 0);
  REQUIRE_THAT(f.Log(),
               ContainsSubstring("Rejected forced mount host: "
                                 "requester=ff000002"));
}

TEST_CASE("Forced resend while the owner already hosts does not replay "
          "HostStart",
          "[PartOne][HostBounds][ForceMountHost]")
{
  ForceMountHostFixture f;
  f.SetMountOwner(kOwnerActor);
  f.RequestHost(kOwnerUser, true);
  REQUIRE(f.partOne.worldState.hosters.at(kHorse) == kOwnerActor);
  REQUIRE(f.CountSent<HostStartMessage>(kOwnerUser) == 1);
  f.SetHorseMovementAge(std::chrono::seconds(0));
  f.ClearOutput();

  // The client resends the forced request every ~500 ms until it observes
  // the host locally. With fresh movement this is a no-op.
  f.RequestHost(kOwnerUser, true);
  f.RequestHost(kOwnerUser, true);

  REQUIRE(f.partOne.worldState.hosters.at(kHorse) == kOwnerActor);
  REQUIRE(f.CountSent<HostStartMessage>(kOwnerUser) == 0);
  REQUIRE_THAT(f.Log(), !ContainsSubstring("Hoster changed"));

  // A stale host (no movement for more than 2 s) is re-sent by the regular
  // rule, so an owner that lost the host locally still recovers it.
  f.SetHorseMovementAge(std::chrono::seconds(3));
  f.RequestHost(kOwnerUser, true);

  REQUIRE(f.partOne.worldState.hosters.at(kHorse) == kOwnerActor);
  REQUIRE(f.CountSent<HostStartMessage>(kOwnerUser) == 1);
}
