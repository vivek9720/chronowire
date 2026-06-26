#include "chronowire/journal.h"

#include <algorithm>
#include <cctype>
#include <sstream>

#include "chronowire/reader.h"

namespace chronowire {
namespace {

std::string trim(std::string text) {
  auto is_space = [](unsigned char ch) { return std::isspace(ch) != 0; };
  text.erase(text.begin(), std::find_if(text.begin(), text.end(),
                                        [&](char ch) { return !is_space(ch); }));
  text.erase(std::find_if(text.rbegin(), text.rend(),
                          [&](char ch) { return !is_space(ch); })
                 .base(),
             text.end());
  return text;
}

std::vector<std::string> splitWords(const std::string& line) {
  std::vector<std::string> words;
  std::string current;
  bool quoted = false;
  bool escaping = false;
  for (char ch : line) {
    if (escaping) {
      current.push_back(ch);
      escaping = false;
      continue;
    }
    if (ch == '\\') {
      escaping = true;
      continue;
    }
    if (ch == '"') {
      quoted = !quoted;
      continue;
    }
    if (std::isspace(static_cast<unsigned char>(ch)) && !quoted) {
      if (!current.empty()) {
        words.push_back(current);
        current.clear();
      }
      continue;
    }
    current.push_back(ch);
  }
  if (!current.empty()) {
    words.push_back(current);
  }
  return words;
}

Result<std::uint64_t> parseU64(const std::string& text, std::size_t line_number) {
  try {
    std::size_t consumed = 0;
    unsigned long long value = std::stoull(text, &consumed, 0);
    if (consumed != text.size()) {
      return Result<std::uint64_t>::failure(
          makeError(ErrorCode::kInvalidSyntax, "invalid integer", line_number));
    }
    return Result<std::uint64_t>::success(static_cast<std::uint64_t>(value));
  } catch (...) {
    return Result<std::uint64_t>::failure(
        makeError(ErrorCode::kInvalidSyntax, "invalid integer", line_number));
  }
}

Result<void> parseFieldToken(JournalRecord& record, const std::string& token,
                             std::size_t line_number) {
  const std::size_t equals = token.find('=');
  if (equals == std::string::npos || equals == 0) {
    return Result<void>::failure(
        makeError(ErrorCode::kInvalidSyntax, "field token missing '='", line_number));
  }
  record.fields[token.substr(0, equals)] = token.substr(equals + 1);
  return Result<void>::success();
}

Result<std::string> readStringTableValue(ByteReader& reader, const std::vector<std::string>& table) {
  auto id = reader.readVarint(table.size() == 0 ? 0 : table.size() - 1);
  if (!id) {
    return Result<std::string>::failure(id.error());
  }
  if (id.value() >= table.size()) {
    return Result<std::string>::failure(
        makeError(ErrorCode::kInvalidSyntax, "string table id out of range", reader.offset()));
  }
  return Result<std::string>::success(table[static_cast<std::size_t>(id.value())]);
}

}  // namespace

const char* journalOpName(JournalOp op) {
  switch (op) {
    case JournalOp::kBegin:
      return "begin";
    case JournalOp::kPut:
      return "put";
    case JournalOp::kErase:
      return "erase";
    case JournalOp::kCommit:
      return "commit";
    case JournalOp::kRollback:
      return "rollback";
  }
  return "unknown";
}

Result<std::vector<JournalPage>> parseJournal(const std::uint8_t* data, std::size_t size) {
  if (data != nullptr && size >= 4 &&
      std::equal(data, data + 4, reinterpret_cast<const std::uint8_t*>("JPG1"))) {
    return parseJournalBinary(data, size);
  }
  std::string text;
  if (data != nullptr && size != 0) {
    text.assign(reinterpret_cast<const char*>(data), size);
  }
  return parseJournalText(text);
}

Result<std::vector<JournalPage>> parseJournalText(const std::string& text) {
  std::istringstream input(text);
  std::string line;
  std::size_t line_number = 0;
  std::vector<JournalPage> pages;
  JournalPage current_page;
  bool saw_page = false;

  while (std::getline(input, line)) {
    ++line_number;
    line = trim(line);
    if (line.empty() || line[0] == '#') {
      continue;
    }
    if (line == "JNL1") {
      continue;
    }

    auto words = splitWords(line);
    if (words.empty()) {
      continue;
    }

    if (words[0] == "page") {
      if (words.size() != 3) {
        return Result<std::vector<JournalPage>>::failure(
            makeError(ErrorCode::kInvalidSyntax, "page expects id and lsn", line_number));
      }
      if (saw_page) {
        pages.push_back(std::move(current_page));
        current_page = JournalPage{};
      }
      auto id = parseU64(words[1], line_number);
      auto lsn = parseU64(words[2], line_number);
      if (!id) {
        return Result<std::vector<JournalPage>>::failure(id.error());
      }
      if (!lsn) {
        return Result<std::vector<JournalPage>>::failure(lsn.error());
      }
      current_page.page_id = static_cast<std::uint32_t>(id.value());
      current_page.lsn = lsn.value();
      saw_page = true;
      continue;
    }

    if (!saw_page) {
      saw_page = true;
      current_page.page_id = 0;
      current_page.lsn = 0;
    }

    if (words[0] == "str") {
      if (words.size() < 3) {
        return Result<std::vector<JournalPage>>::failure(
            makeError(ErrorCode::kInvalidSyntax, "str expects id and value", line_number));
      }
      auto id = parseU64(words[1], line_number);
      if (!id) {
        return Result<std::vector<JournalPage>>::failure(id.error());
      }
      if (id.value() > 4096) {
        return Result<std::vector<JournalPage>>::failure(
            makeError(ErrorCode::kLimitExceeded, "string table id too large", line_number));
      }
      if (current_page.string_table.size() <= id.value()) {
        current_page.string_table.resize(static_cast<std::size_t>(id.value()) + 1);
      }
      current_page.string_table[static_cast<std::size_t>(id.value())] = words[2];
      continue;
    }

    JournalRecord record;
    if (words[0] == "begin") {
      if (words.size() != 2) {
        return Result<std::vector<JournalPage>>::failure(
            makeError(ErrorCode::kInvalidSyntax, "begin expects txid", line_number));
      }
      record.op = JournalOp::kBegin;
    } else if (words[0] == "put") {
      if (words.size() < 3) {
        return Result<std::vector<JournalPage>>::failure(
            makeError(ErrorCode::kInvalidSyntax, "put expects txid, key, fields", line_number));
      }
      record.op = JournalOp::kPut;
      record.key = words[2];
      for (std::size_t i = 3; i < words.size(); ++i) {
        auto field = parseFieldToken(record, words[i], line_number);
        if (!field) {
          return Result<std::vector<JournalPage>>::failure(field.error());
        }
      }
    } else if (words[0] == "erase") {
      if (words.size() != 3) {
        return Result<std::vector<JournalPage>>::failure(
            makeError(ErrorCode::kInvalidSyntax, "erase expects txid and key", line_number));
      }
      record.op = JournalOp::kErase;
      record.key = words[2];
    } else if (words[0] == "commit") {
      if (words.size() != 2) {
        return Result<std::vector<JournalPage>>::failure(
            makeError(ErrorCode::kInvalidSyntax, "commit expects txid", line_number));
      }
      record.op = JournalOp::kCommit;
    } else if (words[0] == "rollback") {
      if (words.size() != 2) {
        return Result<std::vector<JournalPage>>::failure(
            makeError(ErrorCode::kInvalidSyntax, "rollback expects txid", line_number));
      }
      record.op = JournalOp::kRollback;
    } else {
      return Result<std::vector<JournalPage>>::failure(
          makeError(ErrorCode::kInvalidSyntax, "unknown journal record", line_number));
    }

    auto txid = parseU64(words[1], line_number);
    if (!txid) {
      return Result<std::vector<JournalPage>>::failure(txid.error());
    }
    record.txid = txid.value();
    current_page.records.push_back(std::move(record));
  }

  if (saw_page) {
    pages.push_back(std::move(current_page));
  }
  return Result<std::vector<JournalPage>>::success(std::move(pages));
}

Result<std::vector<JournalPage>> parseJournalBinary(const std::uint8_t* data, std::size_t size) {
  ByteReader reader(data, size);
  if (!reader.startsWith("JPG1", 4)) {
    return Result<std::vector<JournalPage>>::failure(
        makeError(ErrorCode::kInvalidMagic, "journal page stream missing JPG1", 0));
  }
  auto skipped = reader.skip(4);
  if (!skipped) {
    return Result<std::vector<JournalPage>>::failure(skipped.error());
  }

  auto page_count = reader.readVarint(256);
  if (!page_count) {
    return Result<std::vector<JournalPage>>::failure(page_count.error());
  }
  std::vector<JournalPage> pages;
  for (std::size_t p = 0; p < page_count.value(); ++p) {
    JournalPage page;
    auto page_id = reader.readLe32();
    auto lsn = reader.readLe64();
    auto string_count = reader.readVarint(4096);
    auto record_count = reader.readVarint(8192);
    if (!page_id) {
      return Result<std::vector<JournalPage>>::failure(page_id.error());
    }
    if (!lsn) {
      return Result<std::vector<JournalPage>>::failure(lsn.error());
    }
    if (!string_count) {
      return Result<std::vector<JournalPage>>::failure(string_count.error());
    }
    if (!record_count) {
      return Result<std::vector<JournalPage>>::failure(record_count.error());
    }
    page.page_id = page_id.value();
    page.lsn = lsn.value();

    for (std::size_t i = 0; i < string_count.value(); ++i) {
      auto length = reader.readVarint(4096);
      if (!length) {
        return Result<std::vector<JournalPage>>::failure(length.error());
      }
      auto value = reader.readString(static_cast<std::size_t>(length.value()));
      if (!value) {
        return Result<std::vector<JournalPage>>::failure(value.error());
      }
      page.string_table.push_back(value.takeValue());
    }

    for (std::size_t i = 0; i < record_count.value(); ++i) {
      auto op = reader.readU8();
      auto txid = reader.readVarint(1ull << 40);
      if (!op) {
        return Result<std::vector<JournalPage>>::failure(op.error());
      }
      if (!txid) {
        return Result<std::vector<JournalPage>>::failure(txid.error());
      }
      JournalRecord record;
      record.txid = txid.value();
      switch (op.value()) {
        case 1:
          record.op = JournalOp::kBegin;
          break;
        case 2:
          record.op = JournalOp::kPut;
          break;
        case 3:
          record.op = JournalOp::kErase;
          break;
        case 4:
          record.op = JournalOp::kCommit;
          break;
        case 5:
          record.op = JournalOp::kRollback;
          break;
        default:
          return Result<std::vector<JournalPage>>::failure(
              makeError(ErrorCode::kInvalidSyntax, "unknown binary journal op", reader.offset()));
      }
      if (record.op == JournalOp::kPut || record.op == JournalOp::kErase) {
        auto key = readStringTableValue(reader, page.string_table);
        if (!key) {
          return Result<std::vector<JournalPage>>::failure(key.error());
        }
        record.key = key.takeValue();
      }
      if (record.op == JournalOp::kPut) {
        auto field_count = reader.readVarint(128);
        if (!field_count) {
          return Result<std::vector<JournalPage>>::failure(field_count.error());
        }
        for (std::size_t f = 0; f < field_count.value(); ++f) {
          auto name = readStringTableValue(reader, page.string_table);
          auto value = readStringTableValue(reader, page.string_table);
          if (!name) {
            return Result<std::vector<JournalPage>>::failure(name.error());
          }
          if (!value) {
            return Result<std::vector<JournalPage>>::failure(value.error());
          }
          record.fields[name.takeValue()] = value.takeValue();
        }
      }
      page.records.push_back(std::move(record));
    }
    pages.push_back(std::move(page));
  }

  return Result<std::vector<JournalPage>>::success(std::move(pages));
}

Result<ReplayState> replayJournal(const std::vector<JournalPage>& pages) {
  ReplayState state;
  std::map<std::uint64_t, std::map<std::string, std::map<std::string, std::string>>> pending_puts;
  std::map<std::uint64_t, std::set<std::string>> pending_erases;

  for (const auto& page : pages) {
    for (const auto& record : page.records) {
      switch (record.op) {
        case JournalOp::kBegin:
          state.open_transactions.insert(record.txid);
          break;
        case JournalOp::kPut:
          if (state.open_transactions.count(record.txid) == 0) {
            state.rows[record.key] = record.fields;
            ++state.applied_records;
          } else {
            pending_puts[record.txid][record.key] = record.fields;
          }
          break;
        case JournalOp::kErase:
          if (state.open_transactions.count(record.txid) == 0) {
            state.rows.erase(record.key);
            ++state.applied_records;
          } else {
            pending_erases[record.txid].insert(record.key);
          }
          break;
        case JournalOp::kCommit:
          if (state.open_transactions.erase(record.txid) == 0) {
            break;
          }
          for (const auto& key : pending_erases[record.txid]) {
            state.rows.erase(key);
            ++state.applied_records;
          }
          for (const auto& item : pending_puts[record.txid]) {
            state.rows[item.first] = item.second;
            ++state.applied_records;
          }
          pending_erases.erase(record.txid);
          pending_puts.erase(record.txid);
          break;
        case JournalOp::kRollback:
          if (state.open_transactions.erase(record.txid) != 0) {
            state.rolled_back_records += pending_puts[record.txid].size();
            state.rolled_back_records += pending_erases[record.txid].size();
          }
          pending_erases.erase(record.txid);
          pending_puts.erase(record.txid);
          break;
      }
    }
  }

  return Result<ReplayState>::success(std::move(state));
}

}  // namespace chronowire
