#pragma once
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

// SRP experimental: tells the gamemode when a RequestSave'd ChangeForm copy
// reached the database driver (IDatabase::Upsert returned without throwing).
//
// Every RequestSave stamps the form with a generation from one monotonic
// counter. A batch handed to ISaveStorage::Upsert holds the latest copy of
// each form, and a later copy always contains the earlier changes, so when
// the batch callback fires each form in it is saved up to the generation
// its copy had. A failed batch changes nothing: those generations stay
// unsaved until a later batch with the same form succeeds.
//
// "Saved" means "survives a process crash" for the file driver (tmp +
// rename) and "acknowledged by the server" for mongodb. Power loss also needs
// fsync in the driver (FileDatabase "databaseFsync").
class SaveGenerationTracker
{
public:
  uint64_t OnRequestSave(uint32_t idx)
  {
    Grow(idx);
    requestedByIdx[idx] = ++lastRequested;
    return requestedByIdx[idx];
  }

  // Called right before Upsert with the non-empty slots of the batch.
  // Returns a batch id for OnBatchSaved.
  uint64_t BeginBatch(const std::vector<uint32_t>& idxs)
  {
    Batch b;
    b.id = ++lastBatch;
    b.forms.reserve(idxs.size());
    for (auto idx : idxs) {
      b.forms.push_back({ idx, GetRequested(idx) });
    }
    inFlight = std::move(b);
    return inFlight->id;
  }

  // Upsert callback: the whole batch reached the driver.
  void OnBatchSaved(uint64_t batchId)
  {
    if (!inFlight || inFlight->id != batchId) {
      return;
    }
    for (auto& [idx, gen] : inFlight->forms) {
      Grow(idx);
      if (gen > savedByIdx[idx]) {
        savedByIdx[idx] = gen;
      }
    }
    inFlight.reset();
  }

  // The storage reported a failure for the in-flight batch.
  void OnBatchFailed() { inFlight.reset(); }

  bool IsSaved(uint32_t idx, uint64_t generation) const
  {
    return generation != 0 && generation <= GetSaved(idx);
  }

  uint64_t GetRequested(uint32_t idx) const
  {
    return idx < requestedByIdx.size() ? requestedByIdx[idx] : 0;
  }

  uint64_t GetSaved(uint32_t idx) const
  {
    return idx < savedByIdx.size() ? savedByIdx[idx] : 0;
  }

  bool HasBatchInFlight() const { return inFlight.has_value(); }

private:
  struct Batch
  {
    uint64_t id = 0;
    std::vector<std::pair<uint32_t, uint64_t>> forms;
  };

  void Grow(uint32_t idx)
  {
    if (requestedByIdx.size() <= idx) {
      requestedByIdx.resize(idx + 1, 0);
      savedByIdx.resize(idx + 1, 0);
    }
  }

  std::vector<uint64_t> requestedByIdx;
  std::vector<uint64_t> savedByIdx;
  uint64_t lastRequested = 0;
  uint64_t lastBatch = 0;
  std::optional<Batch> inFlight;
};
