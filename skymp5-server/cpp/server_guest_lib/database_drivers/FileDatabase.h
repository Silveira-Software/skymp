#pragma once
#include "MpChangeForms.h"
#include <database_drivers/IDatabase.h>
#include <spdlog/spdlog.h>

class FileDatabase
  : public Viet::IDatabase<MpChangeForm, FormDesc, std::vector<FormDesc>>
{
public:
  // SRP experimental: fsync_ = true flushes each file and the directory to
  // the disk before Upsert returns, so a saved generation also survives a
  // power loss (default false: tmp + rename only survives a process crash).
  FileDatabase(std::string directory_,
               std::shared_ptr<spdlog::logger> logger_, bool fsync_ = false);

  void Iterate(const IterateCallback& iterateCallback,
               std::optional<std::vector<FormDesc>> filter) override;

private:
  std::vector<std::optional<MpChangeForm>>&& UpsertImpl(
    std::vector<std::optional<MpChangeForm>>&& changeForms,
    size_t& outNumUpserted) override;

  struct Impl;
  std::shared_ptr<Impl> pImpl;
};
