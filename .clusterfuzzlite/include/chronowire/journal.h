#pragma once

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "chronowire/result.h"

namespace chronowire {

enum class JournalOp {
  kBegin,
  kPut,
  kErase,
  kCommit,
  kRollback,
};

struct JournalRecord {
  JournalOp op = JournalOp::kPut;
  std::uint64_t txid = 0;
  std::string key;
  std::map<std::string, std::string> fields;
};

struct JournalPage {
  std::uint32_t page_id = 0;
  std::uint64_t lsn = 0;
  std::vector<std::string> string_table;
  std::vector<JournalRecord> records;
};

struct ReplayState {
  std::map<std::string, std::map<std::string, std::string>> rows;
  std::set<std::uint64_t> open_transactions;
  std::size_t applied_records = 0;
  std::size_t rolled_back_records = 0;
};

Result<std::vector<JournalPage>> parseJournal(const std::uint8_t* data, std::size_t size);
Result<std::vector<JournalPage>> parseJournalText(const std::string& text);
Result<std::vector<JournalPage>> parseJournalBinary(const std::uint8_t* data, std::size_t size);
Result<ReplayState> replayJournal(const std::vector<JournalPage>& pages);

const char* journalOpName(JournalOp op);

}  // namespace chronowire
