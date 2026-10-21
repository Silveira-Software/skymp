// SRP experimental native: generation-confirmed saves, OnEquip operation id and
// optional fsync. Runs against the real WorldState, AsyncSaveStorage and
// FileDatabase from the fork (no Skyrim data files needed).
#include "FormCallbacks.h"
#include "libespm/Loader.h"
#include "MpActor.h"
#include "MsgType.h"
#include "PartOneListener.h"
#include "SaveGenerationTracker.h"
#include "WorldState.h"
#include "database_drivers/FileDatabase.h"
#include "gamemode_events/ChangeFormsSavedEvent.h"
#include "save_storages/AsyncSaveStorage.h"

#include "MessageSerializerFactory.h"
#include "Messages.h"

#include <catch2/catch_all.hpp>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <random>
#include <set>
#include <simdjson.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {
constexpr uint32_t kActorId = 0xff000001;
constexpr uint32_t kBread = 0x00000d62; // ALCH (food) in the synthetic master below

// Smallest master the espm loader accepts: TES4 header + one ALCH group with a
// food item. No Skyrim data is used.
fs::path WriteSyntheticEsm()
{
  auto put32 = [](std::string& b, uint32_t v) { b.append(reinterpret_cast<const char*>(&v), 4); };
  auto put16 = [](std::string& b, uint16_t v) { b.append(reinterpret_cast<const char*>(&v), 2); };
  auto field = [&](std::string& b, const char* type, const std::string& data) {
    b.append(type, 4);
    put16(b, static_cast<uint16_t>(data.size()));
    b += data;
  };
  auto record = [&](const char* type, uint32_t flags, uint32_t id, const std::string& data) {
    std::string r(type, 4);
    put32(r, static_cast<uint32_t>(data.size()));
    put32(r, flags);
    put32(r, id);
    put32(r, 0);
    put16(r, 44);
    put16(r, 0);
    return r + data;
  };
  std::string hedr;
  float version = 1.7f;
  hedr.append(reinterpret_cast<const char*>(&version), 4);
  put32(hedr, 1);
  put32(hedr, 0xd63);
  std::string tes4Data;
  field(tes4Data, "HEDR", hedr);

  std::string alch;
  field(alch, "EDID", std::string("SrpBread\0", 9));
  float weight = 0.1f;
  field(alch, "DATA", std::string(reinterpret_cast<const char*>(&weight), 4));
  std::string enit;
  put32(enit, 2);       // value
  put32(enit, 0x2);     // flags: food
  put32(enit, 0);       // addiction
  float chance = 0;
  enit.append(reinterpret_cast<const char*>(&chance), 4);
  put32(enit, 0);       // use sound
  field(alch, "ENIT", enit);
  std::string alchRec = record("ALCH", 0, kBread, alch);

  std::string grup("GRUP", 4);
  put32(grup, static_cast<uint32_t>(24 + alchRec.size()));
  grup += "ALCH";
  put32(grup, 0);
  put32(grup, 0);
  put32(grup, 0);
  grup += alchRec;

  auto dir = fs::temp_directory_path() / ("srp-native-exp-esm-" + std::to_string(getpid()));
  fs::create_directories(dir);
  auto path = dir / "SrpTest.esm";
  std::ofstream f(path, std::ios::binary);
  f << record("TES4", 1, 0, tes4Data) << grup;
  return path;
}

espm::Loader& SyntheticLoader()
{
  static espm::Loader loader(std::vector<fs::path>{ WriteSyntheticEsm() });
  return loader;
}

using Storage = Viet::AsyncSaveStorage<MpChangeForm, FormDesc, std::vector<FormDesc>>;

fs::path FreshDir(const std::string& name)
{
  auto p = fs::temp_directory_path() / ("srp-native-exp-" + name + "-" + std::to_string(getpid()));
  fs::remove_all(p);
  fs::create_directories(p);
  return p;
}

struct EventRecorder : public PartOneListener
{
  void OnConnect(Networking::UserId) override {}
  void OnDisconnect(Networking::UserId) override {}
  void OnCustomPacket(Networking::UserId, const simdjson::dom::element&) override {}
  bool OnMpApiEvent(const GameModeEvent& event) override
  {
    auto args = event.GetArgumentsJsonArray();
    events.push_back(std::string(event.GetName()) + args);
    if (std::string(event.GetName()) == "onEatItem") {
      // gamemode-like receipts: block a resend of an opId already applied
      auto j = nlohmann::json::parse(args);
      if (j.size() >= 3) {
        auto opId = j[2].get<std::string>();
        if (!appliedOps.insert(opId).second) {
          return false;
        }
      }
      if (blockPlainEat && j.size() < 3) {
        return false;
      }
    }
    return true;
  }
  std::vector<std::string> events;
  std::set<std::string> appliedOps;
  bool blockPlainEat = false;
};

struct World
{
  explicit World(const fs::path& dir, bool fsync = false)
  {
    ws.AttachEspm(&SyntheticLoader(), FormCallbacks::DoNothing);
    ws.AttachSaveStorage(std::make_shared<Storage>(
      std::make_shared<FileDatabase>(dir.string(), spdlog::default_logger(), fsync),
      spdlog::default_logger(), "", 5));
    ws.listeners.push_back(recorder);
    ws.AddForm(std::make_unique<MpActor>(LocationalData(), FormCallbacks::DoNothing()), kActorId);
  }
  MpActor& Actor() { return ws.GetFormAt<MpActor>(kActorId); }

  void SetBread(uint32_t count, const std::string& marker)
  {
    Inventory inv;
    inv.AddItem(kBread, count);
    Actor().SetInventory(inv);
    Actor().SetPropertyValueDump("srpMarker", nlohmann::json(marker).dump(), false, false);
  }

  bool TickUntilSaved(uint64_t gen, std::chrono::milliseconds timeout = std::chrono::milliseconds(5000))
  {
    auto until = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < until) {
      ws.Tick();
      if (ws.IsChangeFormSaved(kActorId, gen)) {
        return true;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
  }

  WorldState ws;
  std::shared_ptr<EventRecorder> recorder = std::make_shared<EventRecorder>();
};

// Reads the actor's ChangeForm file the way FileDatabase::Iterate would.
std::optional<nlohmann::json> ReadActorFile(const fs::path& dir)
{
  auto p = dir / "changeForms";
  if (!fs::exists(p)) {
    return std::nullopt;
  }
  for (auto& e : fs::directory_iterator(p)) {
    if (e.path().extension() != ".json") {
      continue;
    }
    std::ifstream f(e.path());
    auto j = nlohmann::json::parse(f); // throws if a partial file was seen
    static const auto kDesc =
      FormDesc::FromFormId(kActorId, SyntheticLoader().GetFileNames()).ToString();
    if (j.contains("formDesc") && j["formDesc"].get<std::string>() == kDesc) {
      return j;
    }
  }
  return std::nullopt;
}

uint32_t BreadIn(const nlohmann::json& j)
{
  uint32_t n = 0;
  if (j.contains("inv") && j["inv"].contains("entries")) {
    for (auto& e : j["inv"]["entries"]) {
      if (e["baseId"].get<uint32_t>() == kBread) {
        n += e["count"].get<uint32_t>();
      }
    }
  }
  return n;
}

std::string MarkerIn(const nlohmann::json& j)
{
  if (!j.contains("dynamicFields") || !j["dynamicFields"].contains("srpMarker")) {
    return "";
  }
  return j["dynamicFields"]["srpMarker"].get<std::string>();
}
}

TEST_CASE("SaveGenerationTracker: saved only after the batch with that copy succeeds", "[srp]")
{
  SaveGenerationTracker t;
  auto g1 = t.OnRequestSave(3);
  REQUIRE_FALSE(t.IsSaved(3, g1));

  auto b1 = t.BeginBatch({ 3 });
  auto g2 = t.OnRequestSave(3); // change while the batch is in flight
  t.OnBatchSaved(b1);
  REQUIRE(t.IsSaved(3, g1));
  REQUIRE_FALSE(t.IsSaved(3, g2));

  auto b2 = t.BeginBatch({ 3 });
  t.OnBatchFailed();
  t.OnBatchSaved(b2); // a stale callback after failure changes nothing
  REQUIRE_FALSE(t.IsSaved(3, g2));

  auto b3 = t.BeginBatch({ 3 });
  t.OnBatchSaved(b3);
  REQUIRE(t.IsSaved(3, g2));

  // other forms are independent
  auto g9 = t.OnRequestSave(9);
  REQUIRE_FALSE(t.IsSaved(9, g9));
  REQUIRE_FALSE(t.IsSaved(4, 1));
  REQUIRE_FALSE(t.IsSaved(3, 0));
}

TEST_CASE("WorldState: generation becomes saved after FileDatabase wrote the copy", "[srp]")
{
  for (bool fsync : { false, true }) {
    auto dir = FreshDir(fsync ? "gen-fsync" : "gen");
    World w(dir, fsync);
    w.TickUntilSaved(w.ws.GetChangeFormGeneration(kActorId)); // initial save from Init

    w.SetBread(3, "three");
    auto gen = w.ws.GetChangeFormGeneration(kActorId);
    REQUIRE(gen > 0);
    REQUIRE_FALSE(w.ws.IsChangeFormSaved(kActorId, gen));

    REQUIRE(w.TickUntilSaved(gen));
    auto j = ReadActorFile(dir);
    REQUIRE(j.has_value());
    REQUIRE(BreadIn(*j) == 3);
    REQUIRE(MarkerIn(*j) == "three");

    bool sawEvent = false;
    for (auto& e : w.recorder->events) {
      sawEvent = sawEvent || e.rfind("onChangeFormsSaved[", 0) == 0;
    }
    REQUIRE(sawEvent);
    REQUIRE_FALSE(w.ws.IsChangeFormSaved(0x12345, gen));
  }
}

TEST_CASE("WorldState: inventory and gamemode property of one call land in the same file", "[srp]")
{
  auto dir = FreshDir("same-image");
  World w(dir);
  for (uint32_t i = 1; i <= 20; ++i) {
    w.SetBread(i, "n" + std::to_string(i));
    REQUIRE(w.TickUntilSaved(w.ws.GetChangeFormGeneration(kActorId)));
    auto j = ReadActorFile(dir);
    REQUIRE(j.has_value());
    REQUIRE(BreadIn(*j) == i);
    REQUIRE(MarkerIn(*j) == "n" + std::to_string(i));
  }
}

namespace {
// Child process: a gamemode-like loop. Each operation changes inventory and
// a property in one call, then "sends the ACK" (appends to ack file with
// fsync) either right away (old behavior) or after IsChangeFormSaved.
[[noreturn]] void CrashChild(const fs::path& dir, bool waitForSave)
{
  World w(dir);
  auto ackPath = dir / "acks.txt";
  for (uint32_t i = 1;; ++i) {
    w.SetBread(i, "n" + std::to_string(i));
    auto gen = w.ws.GetChangeFormGeneration(kActorId);
    if (waitForSave) {
      if (!w.TickUntilSaved(gen)) {
        _exit(3);
      }
    } else {
      w.ws.Tick();
    }
    FILE* f = fopen(ackPath.c_str(), "a");
    fprintf(f, "%u\n", i);
    fflush(f);
    fsync(fileno(f));
    fclose(f);
  }
}

struct CrashResult
{
  int runs = 0;
  int ackAheadOfDisk = 0; // client got ACK for an operation not on disk
  int incoherent = 0;     // inventory and property disagree on disk
  int unreadable = 0;     // partial / corrupt ChangeForm file
};

CrashResult RunCrashes(bool waitForSave, int runs)
{
  CrashResult r;
  std::mt19937 rng(waitForSave ? 1234 : 4321);
  std::uniform_int_distribution<int> delayUs(2000, 60000);
  for (int k = 0; k < runs; ++k) {
    auto dir = FreshDir(std::string(waitForSave ? "crash-ack" : "crash-old") + std::to_string(k));
    pid_t pid = fork();
    if (pid == 0) {
      CrashChild(dir, waitForSave);
    }
    std::this_thread::sleep_for(std::chrono::microseconds(delayUs(rng)));
    kill(pid, SIGKILL);
    int status = 0;
    waitpid(pid, &status, 0);
    r.runs++;

    uint32_t lastAck = 0;
    {
      std::ifstream f(dir / "acks.txt");
      uint32_t v;
      while (f >> v) {
        lastAck = v;
      }
    }
    std::optional<nlohmann::json> j;
    try {
      j = ReadActorFile(dir);
    } catch (...) {
      r.unreadable++;
      continue;
    }
    uint32_t onDisk = j ? BreadIn(*j) : 0;
    if (j && MarkerIn(*j) != "" && MarkerIn(*j) != "n" + std::to_string(onDisk)) {
      r.incoherent++;
    }
    if (lastAck > onDisk) {
      r.ackAheadOfDisk++;
    }
    fs::remove_all(dir);
  }
  return r;
}
}

TEST_CASE("Crash (SIGKILL): ACK after IsChangeFormSaved is never ahead of disk", "[srp][crash]")
{
  auto withGen = RunCrashes(true, 60);
  auto old = RunCrashes(false, 60);
  INFO("generation ACK: runs=" << withGen.runs << " ackAhead=" << withGen.ackAheadOfDisk
       << " incoherent=" << withGen.incoherent << " unreadable=" << withGen.unreadable);
  INFO("immediate ACK (current): runs=" << old.runs << " ackAhead=" << old.ackAheadOfDisk
       << " incoherent=" << old.incoherent << " unreadable=" << old.unreadable);
  std::printf("[srp-crash] generation ACK runs=%d ackAhead=%d incoherent=%d unreadable=%d\n",
              withGen.runs, withGen.ackAheadOfDisk, withGen.incoherent, withGen.unreadable);
  std::printf("[srp-crash] immediate ACK runs=%d ackAhead=%d incoherent=%d unreadable=%d\n",
              old.runs, old.ackAheadOfDisk, old.incoherent, old.unreadable);
  REQUIRE(withGen.ackAheadOfDisk == 0);
  REQUIRE(withGen.incoherent == 0);
  REQUIRE(withGen.unreadable == 0);
  REQUIRE(old.incoherent == 0);
  REQUIRE(old.unreadable == 0);
  // Not a requirement, only evidence that the control can fail: the current
  // immediate ACK is expected to be ahead of the disk in some runs.
}

TEST_CASE("OnEquipWithOpId: JSON roundtrip and old OnEquip still parses", "[srp]")
{
  auto factory = MessageSerializerFactory::CreateMessageSerializer();

  OnEquipWithOpIdMessage m;
  m.baseId = kBread;
  m.opId = "sess-1:42";
  SLNet::BitStream bs;
  factory->Serialize(m, bs);
  auto res = factory->Deserialize(bs.GetData(), bs.GetNumberOfBytesUsed());
  REQUIRE(res.has_value());
  REQUIRE(res->msgType == MsgType::OnEquipWithOpId);
  auto* back = reinterpret_cast<OnEquipWithOpIdMessage*>(res->message.get());
  REQUIRE(back->baseId == kBread);
  REQUIRE(back->opId == "sess-1:42");

  std::string json = "{\"t\":" + std::to_string(static_cast<int>(MsgType::OnEquipWithOpId)) +
    ",\"baseId\":" + std::to_string(kBread) + ",\"opId\":\"x:1\"}";
  std::string raw = std::string(1, Networking::MinPacketId) + json;
  auto res2 = factory->Deserialize(reinterpret_cast<const uint8_t*>(raw.data()), raw.size());
  REQUIRE(res2.has_value());
  REQUIRE(reinterpret_cast<OnEquipWithOpIdMessage*>(res2->message.get())->opId == "x:1");

  std::string oldJson = std::string(1, Networking::MinPacketId) + "{\"t\":" +
    std::to_string(static_cast<int>(MsgType::OnEquip)) + ",\"baseId\":" + std::to_string(kBread) + "}";
  auto res3 = factory->Deserialize(reinterpret_cast<const uint8_t*>(oldJson.data()), oldJson.size());
  REQUIRE(res3.has_value());
  REQUIRE(res3->msgType == MsgType::OnEquip);
  REQUIRE(reinterpret_cast<OnEquipMessage*>(res3->message.get())->baseId == kBread);
}

TEST_CASE("MpActor::OnEquip with opId: a resend keeps the item and skips the effect", "[srp]")
{
  auto dir = FreshDir("onequip");
  World w(dir);
  w.SetBread(3, "start");

  REQUIRE(w.Actor().OnEquip(kBread, "sess-1:1"));
  REQUIRE(w.Actor().GetInventory().GetItemCount(kBread) == 2);

  // same intent resent (network retry / client resend after reconnect)
  REQUIRE_FALSE(w.Actor().OnEquip(kBread, "sess-1:1"));
  REQUIRE(w.Actor().GetInventory().GetItemCount(kBread) == 2);

  // a new intent consumes again
  REQUIRE(w.Actor().OnEquip(kBread, "sess-1:2"));
  REQUIRE(w.Actor().GetInventory().GetItemCount(kBread) == 1);

  int eats = 0;
  for (auto& e : w.recorder->events) {
    eats += e.rfind("onEatItem[", 0) == 0;
  }
  REQUIRE(eats == 3);
  REQUIRE(std::find(w.recorder->events.begin(), w.recorder->events.end(),
                    "onEatItem[" + std::to_string(kActorId) + "," + std::to_string(kBread) + ",\"sess-1:1\"]") !=
          w.recorder->events.end());
}

TEST_CASE("MpActor::OnEquip without opId preserves allowed consumption and honors veto", "[srp]")
{
  auto dir = FreshDir("onequip-plain");
  World w(dir);
  w.SetBread(2, "start");
  REQUIRE(w.Actor().OnEquip(kBread));
  REQUIRE(w.Actor().GetInventory().GetItemCount(kBread) == 1);
  REQUIRE(std::find(w.recorder->events.begin(), w.recorder->events.end(),
                    "onEatItem[" + std::to_string(kActorId) + "," + std::to_string(kBread) + "]") !=
          w.recorder->events.end());

  // A gamemode veto must not debit even before the SRP marker exists.
  w.recorder->blockPlainEat = true;
  REQUIRE_FALSE(w.Actor().OnEquip(kBread));
  REQUIRE(w.Actor().GetInventory().GetItemCount(kBread) == 1);
}

TEST_CASE("OnEquip opId and removal reach the disk in the same generation", "[srp]")
{
  auto dir = FreshDir("onequip-disk");
  World w(dir);
  w.SetBread(3, "start");
  REQUIRE(w.Actor().OnEquip(kBread, "sess-1:9"));
  auto gen = w.ws.GetChangeFormGeneration(kActorId);
  REQUIRE(w.TickUntilSaved(gen));
  auto j = ReadActorFile(dir);
  REQUIRE(j.has_value());
  REQUIRE(BreadIn(*j) == 2);
}

TEST_CASE("Consumption transaction: veto/exception cannot save a partial ChangeForm", "[srp][transaction]")
{
  auto dir = FreshDir("transaction-rollback");
  World w(dir, true);
  w.SetBread(3, "before");
  const auto initial = w.ws.GetChangeFormGeneration(kActorId);
  REQUIRE(w.TickUntilSaved(initial));
  REQUIRE_FALSE(w.Actor().RunChangeFormTransaction([&] { w.SetBread(2, "partial"); return false; }));
  REQUIRE(w.Actor().GetInventory().GetItemCount(kBread) == 3);
  REQUIRE(w.ws.GetChangeFormGeneration(kActorId) == initial);
  REQUIRE_THROWS(w.Actor().RunChangeFormTransaction([&]() -> bool { w.SetBread(1, "error"); throw std::runtime_error("injected"); }));
  REQUIRE(w.Actor().GetInventory().GetItemCount(kBread) == 3);
  REQUIRE(w.ws.GetChangeFormGeneration(kActorId) == initial);
  REQUIRE(w.Actor().RunChangeFormTransaction([&] { w.SetBread(2, "committed"); return true; }));
  const auto final = w.ws.GetChangeFormGeneration(kActorId);
  REQUIRE(final == initial + 1);
  REQUIRE(w.TickUntilSaved(final));
  const auto disk = ReadActorFile(dir);
  REQUIRE(disk.has_value());
  REQUIRE(BreadIn(*disk) == 2);
  REQUIRE(MarkerIn(*disk) == "committed");
}

TEST_CASE("Legacy equip veto before character marker preserves durable inventory", "[srp][transaction]")
{
  auto dir = FreshDir("pre-marker-veto");
  World w(dir, true);
  w.SetBread(3, "untouched");
  auto generation = w.ws.GetChangeFormGeneration(kActorId);
  REQUIRE(w.TickUntilSaved(generation));
  w.recorder->blockPlainEat = true;
  REQUIRE_FALSE(w.Actor().OnEquip(kBread));
  REQUIRE(w.Actor().GetInventory().GetItemCount(kBread) == 3);
  REQUIRE(w.ws.GetChangeFormGeneration(kActorId) == generation);
  auto disk = ReadActorFile(dir);
  REQUIRE(disk.has_value());
  REQUIRE(BreadIn(*disk) == 3);
  REQUIRE(MarkerIn(*disk) == "untouched");
}
