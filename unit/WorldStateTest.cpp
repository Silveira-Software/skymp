#include "WorldState.h"
#include "FormCallbacks.h"
#include "MpActor.h"
#include "MpForm.h"
#include "MsgType.h"
#include "PartOne.h"
#include <catch2/catch_all.hpp>
#include <nlohmann/json.hpp>
#include "libespm/Loader.h"
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

using Catch::Matchers::ContainsSubstring;

TEST_CASE("AddForm failures", "[WorldState]")
{
  WorldState worldState;
  worldState.AddForm(std::unique_ptr<MpForm>(new MpForm), 0xff000000);
  REQUIRE_THROWS_WITH(
    worldState.AddForm(std::unique_ptr<MpForm>(new MpForm), 0xff000000),
    ContainsSubstring("Form with id ff000000 already exists"));
}

TEST_CASE("DestroyForm failures", "[WorldState]")
{
  WorldState worldState;
  REQUIRE_THROWS_WITH(
    worldState.DestroyForm(0x12345678),
    ContainsSubstring("Form with id 12345678 doesn't exist"));

  worldState.AddForm(std::unique_ptr<MpForm>(new MpForm), 0x12345678);
  REQUIRE_THROWS_WITH(
    worldState.DestroyForm<MpActor>(0x12345678),
    ContainsSubstring("Expected form 12345678 to be Actor, but got Form"));
}

TEST_CASE("Load ChangeForm of created Actor", "[WorldState]")
{
  WorldState worldState;
  worldState.espmFiles = { "Morrowind.esm", "Tribunal.esm" };

  MpChangeForm changeForm;
  changeForm.recType = MpChangeForm::ACHR;
  changeForm.position = { 1, 2, 3 };
  changeForm.worldOrCellDesc = FormDesc::Tamriel();
  changeForm.baseDesc = { 0xabcd, "Tribunal.esm" };

  worldState.LoadChangeForm(changeForm, FormCallbacks::DoNothing());

  auto& refr = worldState.GetFormAt<MpActor>(0xff000000);
  REQUIRE(refr.GetFormId() == 0xff000000);
  REQUIRE(refr.GetChangeForm().formDesc.ToString() == "0");
  REQUIRE(refr.GetPos() == NiPoint3{ 1, 2, 3 });
  REQUIRE(refr.GetCellOrWorld() == FormDesc::Tamriel());
  REQUIRE(refr.GetBaseId() == 0x0100abcd);
}

TEST_CASE("Load ChangeForm of created Actor with isDisabled=true",
          "[WorldState]")
{
  WorldState worldState;
  worldState.espmFiles = { "Morrowind.esm", "Tribunal.esm" };

  MpChangeForm changeForm;
  changeForm.recType = MpChangeForm::ACHR;
  changeForm.worldOrCellDesc = FormDesc::FromString("dead:Morrowind.esm");
  changeForm.baseDesc = { 0xabcd, "Tribunal.esm" };
  changeForm.isDisabled = true;

  worldState.LoadChangeForm(changeForm, FormCallbacks::DoNothing());

  auto& refr = worldState.GetFormAt<MpActor>(0xff000000);
  REQUIRE(refr.IsDisabled());

  // Disabled actors should not pollute grids during load process
  REQUIRE(worldState.GetGrids().count(0xdead) == 0);
}

TEST_CASE("Load ChangeForm of created Actor with profileId", "[WorldState]")
{
  WorldState worldState;
  worldState.espmFiles = { "Morrowind.esm", "Tribunal.esm" };

  MpChangeForm changeForm;
  changeForm.recType = MpChangeForm::ACHR;
  changeForm.worldOrCellDesc = FormDesc::FromString("dead:Morrowind.esm");
  changeForm.baseDesc = { 0xabcd, "Tribunal.esm" };
  changeForm.isDisabled = true;
  changeForm.profileId = 100;

  REQUIRE(worldState.GetActorsByProfileId(100).empty());
  worldState.LoadChangeForm(changeForm, FormCallbacks::DoNothing());

  REQUIRE(worldState.GetGrids().count(0xdead) == 0);
  REQUIRE(worldState.GetActorsByProfileId(100) ==
          std::set<uint32_t>({ 0xff000000 }));
}

TEST_CASE("Load ChangeForm of modified object", "[WorldState]")
{
  WorldState worldState;
  worldState.espmFiles = { "Skyrim.esm" };

  MpChangeForm changeForm;
  changeForm.formDesc = { 0xeeee, "Skyrim.esm" };
  changeForm.position = { 1, 2, 3 };
  changeForm.worldOrCellDesc = FormDesc::Tamriel();
  changeForm.baseDesc = { 0xabcd, "Skyrim.esm" };

  auto newRefr = new MpObjectReference(
    LocationalData(), FormCallbacks::DoNothing(), 0x0000abcd, "STAT");
  worldState.AddForm(std::unique_ptr<MpObjectReference>(newRefr), 0xeeee);

  worldState.LoadChangeForm(changeForm, FormCallbacks::DoNothing());
  auto& refr = worldState.GetFormAt<MpObjectReference>(0xeeee);
  REQUIRE(refr.GetFormId() == 0xeeee);
  REQUIRE(refr.GetChangeForm().formDesc.ToString() == "eeee:Skyrim.esm");
  REQUIRE(refr.GetPos() == NiPoint3{ 1, 2, 3 });
  REQUIRE(refr.GetCellOrWorld() == FormDesc::Tamriel());
  REQUIRE(refr.GetBaseId() == 0x0000abcd);
  REQUIRE(refr.Type() == std::string("ObjectReference"));
  REQUIRE(&refr == newRefr);
}

TEST_CASE("Load ChangeForm of modified object with changed baseType",
          "[WorldState]")
{
  WorldState worldState;
  worldState.espmFiles = { "Skyrim.esm" };
  auto newRefr = new MpObjectReference(
    LocationalData(), FormCallbacks::DoNothing(), 0x0000ded0, "STAT");
  worldState.AddForm(std::unique_ptr<MpObjectReference>(newRefr), 0xeeee);

  MpChangeForm changeForm;
  changeForm.formDesc = { 0xeeee, "Skyrim.esm" };
  changeForm.baseDesc = { 0xabcd, "Skyrim.esm" };

  worldState.LoadChangeForm(changeForm, FormCallbacks::DoNothing());

  // Currently, baseId is not changed. I'm not sure if it should be changed.
  REQUIRE(newRefr->GetBaseId() == 0x0000ded0);

  // REQUIRE_THROWS_WITH(
  //   worldState.LoadChangeForm(changeForm, FormCallbacks::DoNothing()),
  //   ContainsSubstring("Anomally, baseId should never change (ded0 =>
  //   abcd)"));
}

extern PartOne& GetPartOne();

TEST_CASE("Loads VirtualMachine with all scripts", "[WorldState]")
{
  auto& p = GetPartOne();
  p.worldState.GetPapyrusVm();
}

TEST_CASE("HasEspmFile is working correctly", "[WorldState]")
{
  WorldState worldState;
  worldState.espmFiles = { "file1", "file2" };
  REQUIRE(worldState.HasEspmFile("file1"));
  REQUIRE(worldState.HasEspmFile("file2"));
  REQUIRE_FALSE(worldState.HasEspmFile("BlowSkyrimModIndustry.exe"));
}

TEST_CASE("Chunk loading rejects unresolved plugin IDs without touching runtime forms",
          "[WorldState][MappedIdRegression]")
{
  auto word = [](char* p, uint32_t value) {
    std::memcpy(p, &value, sizeof(value));
  };
  auto record = [&](const char* type, uint32_t id,
                    const std::vector<char>& payload) {
    std::vector<char> bytes(24 + payload.size(), '\0');
    std::memcpy(bytes.data(), type, 4);
    word(bytes.data() + 4, static_cast<uint32_t>(payload.size()));
    word(bytes.data() + 12, id);
    std::copy(payload.begin(), payload.end(), bytes.begin() + 24);
    return bytes;
  };
  auto append = [](std::vector<char>& to, const std::vector<char>& from) {
    to.insert(to.end(), from.begin(), from.end());
  };
  // One valid reference and two invalid IDs share their low 24 bits.
  // Rejecting them must not clamp, remap or collide with a runtime object.
  std::vector<char> payload(6 + 4 + 6 + 24, '\0');
  std::memcpy(payload.data(), "NAME", 4);
  uint16_t fieldSize = 4;
  std::memcpy(payload.data() + 4, &fieldSize, 2);
  word(payload.data() + 6, 0x2000);
  std::memcpy(payload.data() + 10, "DATA", 4);
  fieldSize = 24;
  std::memcpy(payload.data() + 14, &fieldSize, 2);
  std::vector<char> children;
  for (uint32_t id : {0xff005ea2U, 0x01005ea2U, 0x00005ea2U})
    append(children, record("REFR", id, payload));
  std::vector<char> group(24, '\0');
  std::memcpy(group.data(), "GRUP", 4);
  word(group.data() + 4, static_cast<uint32_t>(24 + children.size()));
  word(group.data() + 8, 0x1000);
  word(group.data() + 12, 6); // CELL_CHILDREN
  append(group, children);
  auto bytes = record("TES4", 0, {});
  append(bytes, record("ACTI", 0x2000, {}));
  append(bytes, record("CELL", 0x1000, {}));
  append(bytes, group);

  struct TemporaryPlugin
  {
    std::filesystem::path path = std::filesystem::temp_directory_path() /
      ("mapped-id-regression-" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()) + ".esm");
    ~TemporaryPlugin() { std::error_code ec; std::filesystem::remove(path, ec); }
  } file;
  { std::ofstream out(file.path, std::ios::binary);
    out.write(bytes.data(), bytes.size()); REQUIRE(out.good()); }
  espm::Loader loader(std::vector<std::filesystem::path>{file.path});
  WorldState world;
  world.AttachEspm(&loader, [] { return FormCallbacks::DoNothing(); });
  auto runtime = std::make_unique<MpForm>();
  const auto* runtimePtr = runtime.get();
  world.AddForm(std::move(runtime), 0xff005ea2U);
  REQUIRE_NOTHROW(world.GetNeighborsByPosition(0x1000, 0, 0));
  REQUIRE(world.LookupFormById(0xff005ea2U).get() == runtimePtr);
  REQUIRE_FALSE(world.LookupFormById(0x01005ea2U));
  auto& valid = world.GetFormAt<MpObjectReference>(0x00005ea2U);
  REQUIRE(valid.GetBaseId() == 0x2000);
  REQUIRE(world.GetNeighborsByPosition(0x1000, 0, 0).count(&valid) == 1);
}
