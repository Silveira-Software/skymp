#include "libespm/QUST.h"
#include "libespm/ReadUnaligned.h"
#include "libespm/RecordHeaderAccess.h"
#include <cstring>
#include <spdlog/spdlog.h>

namespace espm {

std::vector<QUST::Alias> QUST::GetQuestAliases(
  CompressedFieldsCache& compressedFieldsCache) const noexcept
{
  std::vector<Alias> result;

  RecordHeaderAccess::IterateFields(
    this,
    [&](const char* type, uint32_t dataSize, const char* data) {
      if (!std::memcmp(type, "ALST", 4)) {
        result.push_back(Alias());
        result.back().aliasID = ReadFieldScalar<uint32_t>(data, dataSize);
      } else if (!std::memcmp(type, "ALLS", 4)) {
        result.push_back(Alias());
        result.back().locationID = ReadFieldScalar<uint32_t>(data, dataSize);
      } else if (!std::memcmp(type, "ALID", 4)) {
        result.back().aliasName = data;
      } else if (!std::memcmp(type, "FNAM", 4) && result.size() != 0) {
        result.back().flags =
          ReadFieldScalar<Alias::AliasFlags>(data, dataSize);
      } else if (!std::memcmp(type, "ALFI", 4)) {
        result.back().forcedIntoAlias =
          ReadFieldScalar<uint32_t>(data, dataSize);
      } else if (!std::memcmp(type, "ALCO", 4)) {
        result.back().aliasCreatedObject =
          ReadFieldScalar<uint32_t>(data, dataSize);
      } else if (!std::memcmp(type, "ALCA", 4)) {
        result.back().createAt = ReadFieldScalar<uint32_t>(data, dataSize);
      } else if (!std::memcmp(type, "ALCL", 4)) {
        result.back().createLevel =
          ReadFieldScalar<Alias::Level>(data, dataSize);
      } else if (!std::memcmp(type, "ALEQ", 4)) {
        result.back().externalAliasReference =
          ReadFieldScalar<uint32_t>(data, dataSize);
      } else if (!std::memcmp(type, "ALEA", 4)) {
        result.back().externalAlias = ReadFieldScalar<uint32_t>(data, dataSize);
      } else if (!std::memcmp(type, "ALFA", 4)) {
        result.back().referenceAliasLocation =
          ReadFieldScalar<uint32_t>(data, dataSize);
      } else if (!std::memcmp(type, "KNAM", 4)) {
        result.back().keyword = ReadFieldScalar<uint32_t>(data, dataSize);
      } else if (!std::memcmp(type, "ALRT", 4)) {
        result.back().aliasReferenceType =
          ReadFieldScalar<uint32_t>(data, dataSize);
      } else if (!std::memcmp(type, "ALFE", 4)) {
        result.back().fromEvent = data;
      } else if (!std::memcmp(type, "ALFD", 4)) {
        result.back().eventData = data;
      } else if (!std::memcmp(type, "ALFL", 4)) {
        result.back().aliasForcedLocation =
          ReadFieldScalar<uint32_t>(data, dataSize);
      } else if (!std::memcmp(type, "ALFR", 4)) {
        result.back().aliasForcedReference =
          ReadFieldScalar<uint32_t>(data, dataSize);
      } else if (!std::memcmp(type, "ALNA", 4)) {
        result.back().nearAlias = ReadFieldScalar<uint32_t>(data, dataSize);
      } else if (!std::memcmp(type, "ALNT", 4)) {
        result.back().nearType = ReadFieldScalar<uint32_t>(data, dataSize);
      } else if (!std::memcmp(type, "ALUA", 4)) {
        result.back().aliasUniqueActor =
          ReadFieldScalar<uint32_t>(data, dataSize);
      } else if (!std::memcmp(type, "KSIZ", 4)) {
        result.back().keywordsCount = ReadFieldScalar<uint32_t>(data, dataSize);
      } else if (!std::memcmp(type, "KWDA", 4)) {
        result.back().keywords = GetKeywordIds(compressedFieldsCache);
      } else if (!std::memcmp(type, "COCT", 4)) {
        result.back().containersCount =
          ReadFieldScalar<uint32_t>(data, dataSize);
      } else if (!std::memcmp(type, "CNTO", 4)) {
        result.back().containers.push_back(
          ReadFieldScalar<Alias::Container>(data, dataSize));
      } else if (!std::memcmp(type, "SPOR", 4)) {
        result.back().spectatorOverride =
          ReadFieldScalar<uint32_t>(data, dataSize);
      } else if (!std::memcmp(type, "OCOR", 4)) {
        result.back().observeDeadBodyOverride =
          ReadFieldScalar<uint32_t>(data, dataSize);
      } else if (!std::memcmp(type, "GWOR", 4)) {
        result.back().guardWarnOverride =
          ReadFieldScalar<uint32_t>(data, dataSize);
      } else if (!std::memcmp(type, "ECOR", 4)) {
        result.back().combatOverride =
          ReadFieldScalar<uint32_t>(data, dataSize);
      } else if (!std::memcmp(type, "ALDN", 4)) {
        result.back().displayName = ReadFieldScalar<uint32_t>(data, dataSize);
      } else if (!std::memcmp(type, "ALSP", 4)) {
        result.back().aliasSpells.push_back(
          ReadFieldScalar<uint32_t>(data, dataSize));
      } else if (!std::memcmp(type, "ALFC", 4)) {
        result.back().aliasFactions.push_back(
          ReadFieldScalar<uint32_t>(data, dataSize));
      } else if (!std::memcmp(type, "ALPC", 4)) {
        result.back().aliasPackageData.push_back(
          ReadFieldScalar<uint32_t>(data, dataSize));
      } else if (!std::memcmp(type, "VTCK", 4)) {
        result.back().voiceType = ReadFieldScalar<uint32_t>(data, dataSize);
      }
    },
    compressedFieldsCache);
  return result;
}

std::vector<QUST::QuestObjective> QUST::GetQuestObjectives(
  CompressedFieldsCache& compressedFieldsCache) const noexcept
{
  std::vector<QuestObjective> result;

  // FNAM also can occur in Alias, so we need to skip it when last Objective
  // will be filled
  bool backFlagsEntered = false;

  RecordHeaderAccess::IterateFields(
    this,
    [&](const char* type, uint32_t dataSize, const char* data) {
      if (!std::memcmp(type, "QOBJ", 4)) {
        backFlagsEntered = false;
        result.push_back(QuestObjective());
        result.back().index = ReadFieldScalar<int16_t>(data, dataSize);
      } else if (!std::memcmp(type, "FNAM", 4) && result.size() != 0 &&
                 backFlagsEntered == false) {
        result.back().flags = ReadFieldScalar<int32_t>(data, dataSize);
        backFlagsEntered = true;
      } else if (!std::memcmp(type, "NNAM", 4) && result.size() != 0) {
        result.back().nodeName = data;
      } else if (!std::memcmp(type, "QSTA", 4) && result.size() != 0) {
        result.back().questTargets.push_back(
          ReadFieldScalar<QuestObjective::QuestTarget>(data, dataSize));
      }
    },
    compressedFieldsCache);
  return result;
}

std::vector<QUST::QuestStage> QUST::GetQuestStages(
  CompressedFieldsCache& compressedFieldsCache) const noexcept
{
  std::vector<QuestStage> result;

  RecordHeaderAccess::IterateFields(
    this,
    [&](const char* type, uint32_t dataSize, const char* data) {
      if (!std::memcmp(type, "INDX", 4)) {
        result.push_back(QuestStage());
        result.back().journalIndex =
          reinterpret_cast<const QuestStage::JournalIndex*>(data);
      } else if (!std::memcmp(type, "QSDT", 4)) {
        result.back().logEntries.push_back(QuestStage::QuestLogEntry());
        result.back().logEntries.back().logFlags =
          ReadFieldScalar<QuestStage::QuestLogEntry::LogFlags>(data, dataSize);
      } else if (!std::memcmp(type, "CNAM", 4)) {
        result.back().logEntries.back().journalEntry = data;
      }
    },
    compressedFieldsCache);
  return result;
}

QUST::Data QUST::GetData(
  CompressedFieldsCache& compressedFieldsCache) const noexcept
{
  Data result;

  int questStageID = -1;
  int questLogEntryID = -1;

  RecordHeaderAccess::IterateFields(
    this,
    [&](const char* type, uint32_t dataSize, const char* data) {
      if (!std::memcmp(type, "EDID", 4)) {
        result.editorId = data;
      } else if (!std::memcmp(type, "VMAD", 4)) {
        GetScriptData(&result.scriptData, compressedFieldsCache);
      } else if (!std::memcmp(type, "FULL", 4)) {
        result.questName = data;
      } else if (!std::memcmp(type, "DNAM", 4)) {
        result.questData = reinterpret_cast<const QuestData*>(data);
      } else if (!std::memcmp(type, "ENAM", 4)) {
        result.eventName = data;
      } else if (!std::memcmp(type, "QTGL", 4)) {
        result.displayGlobals = ReadFieldScalar<uint32_t>(data, dataSize);
      } else if (!std::memcmp(type, "FLTR", 4)) {
        result.objectWindowFilter = data;
      } else if (!std::memcmp(type, "QuestObjectives", 4)) {
        result.nextAliasId = ReadFieldScalar<uint32_t>(data, dataSize);
      } else if (!std::memcmp(type, "ANAM", 4)) {
        result.nextAliasId = ReadFieldScalar<uint32_t>(data, dataSize);
      } else if (!std::memcmp(type, "Aliases", 4)) {
        result.nextAliasId = ReadFieldScalar<uint32_t>(data, dataSize);
      }
    },
    compressedFieldsCache);

  result.questStages = GetQuestStages(compressedFieldsCache);
  result.questObjectives = GetQuestObjectives(compressedFieldsCache);
  result.aliases = GetQuestAliases(compressedFieldsCache);

  return result;
}

}
