#include "FileDatabase.h"
#include <filesystem>
#include <fstream>
#include <save_storages/AsyncSaveStorage.h>
#include <unordered_set>

#ifndef WIN32
#include <fcntl.h>
#include <unistd.h>
#endif

namespace {
// Flushes a file or a directory to the disk. Throws on failure so the batch
// is reported as failed instead of saved.
void FsyncPath(const std::filesystem::path& path, bool isDirectory)
{
#ifndef WIN32
  int fd = ::open(path.c_str(), isDirectory ? (O_RDONLY | O_DIRECTORY)
                                            : O_RDONLY);
  if (fd < 0) {
    throw std::runtime_error(
      fmt::format("Unable to open {} for fsync", path.string()));
  }
  int res = ::fsync(fd);
  ::close(fd);
  if (res != 0) {
    throw std::runtime_error(fmt::format("fsync failed for {}", path.string()));
  }
#else
  (void)path;
  (void)isDirectory;
  throw std::runtime_error("databaseFsync is not supported on Windows");
#endif
}
}

struct FileDatabase::Impl
{
  const std::filesystem::path changeFormsDirectory;
  const std::shared_ptr<spdlog::logger> logger;
  const bool fsync = false;
};

FileDatabase::FileDatabase(std::string directory_,
                           std::shared_ptr<spdlog::logger> logger_,
                           bool fsync_)
{
  std::filesystem::path p = directory_;
  p /= "changeForms";

  pImpl.reset(new Impl{ p, logger_, fsync_ });
  std::filesystem::create_directories(p);
}

std::vector<std::optional<MpChangeForm>>&& FileDatabase::UpsertImpl(
  std::vector<std::optional<MpChangeForm>>&& changeForms,
  size_t& outNumUpserted)
{
  try {
    std::filesystem::path p = pImpl->changeFormsDirectory;
    size_t nUpserted = 0;

    for (auto& changeForm : changeForms) {
      if (changeForm == std::nullopt) {
        continue;
      }

      std::string fileName = changeForm->formDesc.ToString('_') + ".json";
      auto tempFilePath = p / (fileName + ".tmp"), filePath = p / fileName;

      std::ofstream f(tempFilePath);
      if (f) {
        f << MpChangeForm::ToJson(*changeForm).dump(2);
      }

      bool wasOpen = f.is_open();

      if (!f.fail()) {
        f.close();

        if (pImpl->fsync && !f.fail()) {
          FsyncPath(tempFilePath, false);
        }

        std::error_code errorCode;
        std::filesystem::rename(tempFilePath, filePath, errorCode);
        if (errorCode) {
          throw std::runtime_error(
            fmt::format("Unable to rename {} to {}: {}", tempFilePath.string(),
                        filePath.string(), errorCode.message()));
        }
      }

      if (!wasOpen) {
        throw std::runtime_error(
          fmt::format("Unable to open file {}", filePath.string()));
      } else if (!f) {
        throw std::runtime_error(fmt::format(
          "Unknown error while writing file {}", filePath.string()));
      } else {
        ++nUpserted;
      }
    }

    if (pImpl->fsync && nUpserted > 0) {
      // makes the renames themselves durable
      FsyncPath(p, true);
    }

    outNumUpserted = nUpserted;
    return std::move(changeForms);
  } catch (std::exception& e) {
    throw Viet::AsyncSaveStorage<
      MpChangeForm, FormDesc,
      std::vector<FormDesc>>::UpsertFailedException(std::move(changeForms),
                                                    e.what());
  }
}

void FileDatabase::Iterate(const IterateCallback& iterateCallback,
                           std::optional<std::vector<FormDesc>> filter)
{
  try {

    auto p = pImpl->changeFormsDirectory;

    simdjson::dom::parser parser;

    if (!std::filesystem::exists(p)) {
      return;
    }

    std::optional<std::unordered_set<std::string>> filterSet;
    if (filter) {
      std::unordered_set<std::string>& value = filterSet.emplace();
      for (const auto& desc : *filter) {
        value.insert(desc.ToString());
      }
    }

    for (auto& entry : std::filesystem::directory_iterator(p)) {
      try {
        if (entry.path().extension() != ".json") {
          continue;
        }

        std::ifstream t(entry.path());
        std::string jsonDump((std::istreambuf_iterator<char>(t)),
                             std::istreambuf_iterator<char>());

        auto result = parser.parse(jsonDump).value();
        auto changeForm = MpChangeForm::JsonToChangeForm(result);

        if (filterSet) {
          if (filterSet->find(changeForm.formDesc.ToString()) ==
              filterSet->end()) {
            continue;
          }
        }

        iterateCallback(changeForm);
      } catch (std::exception& e) {
        pImpl->logger->error("Parsing of {} failed with {}",
                             entry.path().string(), e.what());
      }
    }

  } catch (std::exception& e) {
    throw Viet::AsyncSaveStorage<
      MpChangeForm, FormDesc,
      std::vector<FormDesc>>::IterateFailedException(std::move(filter),
                                                     e.what());
  }
}
