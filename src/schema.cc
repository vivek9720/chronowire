#include "chronowire/schema.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>
#include <utility>

#include "chronowire/checksum.h"
#include "chronowire/journal.h"
#include "chronowire/reader.h"
#include "chronowire/stream.h"

namespace chronowire {
namespace {

void addDiagnostic(SchemaProfile& profile, DiagnosticSeverity severity,
                   std::string category, std::string message,
                   std::size_t offset = 0) {
  profile.diagnostics.push_back(
      Diagnostic{severity, std::move(category), std::move(message), offset});
}

bool isValidMetadataKey(const std::string& key) {
  if (key.empty()) {
    return false;
  }
  for (char ch : key) {
    const unsigned char uch = static_cast<unsigned char>(ch);
    if (!std::isalnum(uch) && ch != '_' && ch != '-' && ch != '.') {
      return false;
    }
  }
  return true;
}

std::string normalizeIdentifier(std::string text) {
  std::string normalized;
  normalized.reserve(text.size());
  bool previous_separator = false;
  for (char ch : text) {
    const unsigned char uch = static_cast<unsigned char>(ch);
    if (std::isalnum(uch)) {
      normalized.push_back(static_cast<char>(std::tolower(uch)));
      previous_separator = false;
    } else if (!previous_separator) {
      normalized.push_back('_');
      previous_separator = true;
    }
  }
  while (!normalized.empty() && normalized.back() == '_') {
    normalized.pop_back();
  }
  while (!normalized.empty() && normalized.front() == '_') {
    normalized.erase(normalized.begin());
  }
  return normalized.empty() ? "default" : normalized;
}

std::string keyspaceForRecordKey(const std::string& key) {
  const std::size_t colon = key.find(':');
  const std::size_t dot = key.find('.');
  std::size_t split = std::string::npos;
  if (colon != std::string::npos && dot != std::string::npos) {
    split = std::min(colon, dot);
  } else if (colon != std::string::npos) {
    split = colon;
  } else if (dot != std::string::npos) {
    split = dot;
  }
  if (split == std::string::npos || split == 0) {
    return normalizeIdentifier(key);
  }
  return normalizeIdentifier(key.substr(0, split));
}

bool looksAbsoluteOrExternalInclude(const std::string& target) {
  if (target.empty()) {
    return true;
  }
  if (target.size() >= 2 && std::isalpha(static_cast<unsigned char>(target[0])) &&
      target[1] == ':') {
    return true;
  }
  if (!target.empty() && (target[0] == '/' || target[0] == '\\')) {
    return true;
  }
  if (target.find("://") != std::string::npos) {
    return true;
  }
  if (target.find("..") != std::string::npos) {
    return true;
  }
  return false;
}

std::uint32_t mixSignature(std::uint32_t state, const std::string& text) {
  const std::uint32_t item = checksum32(text);
  state ^= item + 0x9e3779b9u + (state << 6) + (state >> 2);
  return state;
}

std::uint32_t mixSignature(std::uint32_t state, std::uint64_t value) {
  std::string bytes;
  for (int i = 0; i < 8; ++i) {
    bytes.push_back(static_cast<char>((value >> (i * 8)) & 0xff));
  }
  return mixSignature(state, bytes);
}

void updateSignatureFromSections(SchemaProfile& profile) {
  std::uint32_t signature = 0x43574a42u;
  for (const auto& section : profile.sections) {
    signature = mixSignature(signature, section.name);
    signature = mixSignature(signature, sectionTypeName(section.type));
    signature = mixSignature(signature, section.payload_size);
    signature = mixSignature(signature, section.has_checksum ? 1 : 0);
  }
  for (const auto& item : profile.config_keys) {
    signature = mixSignature(signature, item.first);
    signature = mixSignature(signature, item.second.assignments);
    signature = mixSignature(signature, item.second.blocks);
    signature = mixSignature(signature, item.second.expression_nodes);
  }
  for (const auto& item : profile.keyspaces) {
    signature = mixSignature(signature, item.first);
    signature = mixSignature(signature, item.second.put_records);
    signature = mixSignature(signature, item.second.erase_records);
    for (const auto& field : item.second.field_names) {
      signature = mixSignature(signature, field);
    }
  }
  signature = mixSignature(signature, profile.stream.frames);
  signature = mixSignature(signature, profile.stream.reconstructed_messages);
  signature = mixSignature(signature, profile.stream.incomplete_messages);
  signature = mixSignature(signature, profile.begin_records);
  signature = mixSignature(signature, profile.commit_records);
  signature = mixSignature(signature, profile.rollback_records);
  profile.stable_signature = signature;
}

void inspectMetadata(const Bundle& bundle, SchemaProfile& profile) {
  for (const auto& item : bundle.metadata) {
    if (!isValidMetadataKey(item.first)) {
      addDiagnostic(profile, DiagnosticSeverity::kWarning, "metadata",
                    "metadata key contains unusual characters");
    }
    if (item.second.size() > 512) {
      addDiagnostic(profile, DiagnosticSeverity::kWarning, "metadata",
                    "metadata value is unusually long");
    }
  }
  if (bundle.metadata.count("producer") == 0) {
    addDiagnostic(profile, DiagnosticSeverity::kInfo, "metadata",
                  "bundle does not declare a producer metadata value");
  }
}

void inspectConfigSection(const Section& section, SchemaProfile& profile,
                          SchemaOptions options) {
  const std::string text = stringFromBytes(section.payload);
  auto document = parseConfig(text);
  if (!document) {
    addDiagnostic(profile, DiagnosticSeverity::kError, "config",
                  "configuration section failed to parse", document.error().offset);
    return;
  }

  if (document.value().includes.size() > options.max_include_records) {
    addDiagnostic(profile, DiagnosticSeverity::kWarning, "config",
                  "configuration contains many include records");
  }
  for (const auto& include : document.value().includes) {
    if (looksAbsoluteOrExternalInclude(include.target)) {
      addDiagnostic(profile, DiagnosticSeverity::kWarning, "config",
                    "include target is not a simple data-only relative reference",
                    include.source_offset);
    }
  }
  for (const auto& entry : document.value().entries) {
    collectConfigEntryStats(entry, "", profile, options);
  }
}

void inspectJournalRecord(const JournalRecord& record, SchemaProfile& profile,
                          std::set<std::uint64_t>& open_transactions) {
  switch (record.op) {
    case JournalOp::kBegin:
      ++profile.begin_records;
      open_transactions.insert(record.txid);
      break;
    case JournalOp::kPut: {
      auto& stats = profile.keyspaces[keyspaceForRecordKey(record.key)];
      ++stats.put_records;
      for (const auto& field : record.fields) {
        stats.field_names.insert(normalizeIdentifier(field.first));
      }
      break;
    }
    case JournalOp::kErase:
      ++profile.keyspaces[keyspaceForRecordKey(record.key)].erase_records;
      break;
    case JournalOp::kCommit:
      ++profile.commit_records;
      open_transactions.erase(record.txid);
      break;
    case JournalOp::kRollback:
      ++profile.rollback_records;
      open_transactions.erase(record.txid);
      break;
  }
}

void inspectJournalSection(const Section& section, SchemaProfile& profile) {
  auto pages = parseJournal(section.payload.data(), section.payload.size());
  if (!pages) {
    addDiagnostic(profile, DiagnosticSeverity::kError, "journal",
                  "journal section failed to parse", pages.error().offset);
    return;
  }

  std::set<std::uint64_t> open_transactions;
  std::uint64_t previous_lsn = 0;
  bool first_page = true;
  for (const auto& page : pages.value()) {
    if (!first_page && page.lsn < previous_lsn) {
      addDiagnostic(profile, DiagnosticSeverity::kWarning, "journal",
                    "journal page LSN moved backwards", page.lsn);
    }
    first_page = false;
    previous_lsn = page.lsn;
    for (const auto& record : page.records) {
      inspectJournalRecord(record, profile, open_transactions);
    }
  }

  auto replay = replayJournal(pages.value());
  if (!replay) {
    addDiagnostic(profile, DiagnosticSeverity::kError, "journal",
                  "journal replay failed", replay.error().offset);
  } else if (!replay.value().open_transactions.empty()) {
    addDiagnostic(profile, DiagnosticSeverity::kInfo, "journal",
                  "journal replay finished with open transactions");
  }
  profile.dangling_transactions += open_transactions.size();
}

void inspectStreamSection(const Section& section, SchemaProfile& profile,
                          SchemaOptions options) {
  auto frames = parseFrames(section.payload.data(), section.payload.size());
  if (!frames) {
    addDiagnostic(profile, DiagnosticSeverity::kError, "stream",
                  "stream section failed to parse", frames.error().offset);
    return;
  }

  std::map<std::uint64_t, std::set<std::uint32_t>> seen_sequences;
  std::map<std::uint64_t, std::uint32_t> expected_totals;
  for (const auto& frame : frames.value()) {
    ++profile.stream.frames;
    profile.stream.payload_bytes += frame.payload.size();
    if ((frame.flags & 0x2u) != 0) {
      ++profile.stream.compressed_flag_mentions;
    }
    seen_sequences[frame.message_id].insert(frame.sequence);
    expected_totals[frame.message_id] = frame.total;
    if (frame.total > options.max_stream_fragments_per_message) {
      addDiagnostic(profile, DiagnosticSeverity::kWarning, "stream",
                    "stream message declares many fragments", frame.sequence);
    }
  }

  for (const auto& item : expected_totals) {
    const auto found = seen_sequences.find(item.first);
    if (found == seen_sequences.end() ||
        found->second.size() < static_cast<std::size_t>(item.second)) {
      ++profile.stream.incomplete_messages;
    }
  }

  auto messages = reconstructMessages(frames.value());
  if (!messages) {
    addDiagnostic(profile, DiagnosticSeverity::kError, "stream",
                  "stream reconstruction failed", messages.error().offset);
    return;
  }
  profile.stream.reconstructed_messages += messages.value().size();
}

void inspectSectionInventory(const Bundle& bundle, SchemaProfile& profile,
                             SchemaOptions options) {
  bool saw_config = false;
  bool saw_journal = false;
  bool saw_stream = false;
  std::set<std::string> names;

  for (const auto& section : bundle.sections) {
    SectionStats stats;
    stats.name = section.name;
    stats.type = section.type;
    stats.payload_size = section.payload.size();
    stats.has_checksum = section.checksum != 0;
    profile.sections.push_back(stats);

    if (!names.insert(section.name).second) {
      addDiagnostic(profile, DiagnosticSeverity::kWarning, "bundle",
                    "duplicate section name encountered");
    }
    if (section.payload.empty()) {
      addDiagnostic(profile, DiagnosticSeverity::kInfo, "bundle",
                    "section has an empty payload");
    }
    if (section.type == SectionType::kConfig) {
      saw_config = true;
    } else if (section.type == SectionType::kJournal) {
      saw_journal = true;
    } else if (section.type == SectionType::kStream) {
      saw_stream = true;
    } else if (section.type == SectionType::kUnknown) {
      addDiagnostic(profile, DiagnosticSeverity::kError, "bundle",
                    "section type is unknown");
    }
  }

  if (options.require_config_section && !saw_config) {
    addDiagnostic(profile, DiagnosticSeverity::kError, "bundle",
                  "required config section is missing");
  }
  if (options.require_journal_section && !saw_journal) {
    addDiagnostic(profile, DiagnosticSeverity::kError, "bundle",
                  "required journal section is missing");
  }
  if (options.require_stream_section && !saw_stream) {
    addDiagnostic(profile, DiagnosticSeverity::kError, "bundle",
                  "required stream section is missing");
  }
}

std::string severityPrefix(const Diagnostic& diagnostic) {
  std::string text = diagnosticSeverityName(diagnostic.severity);
  text += ":";
  text += diagnostic.category;
  return text;
}

}  // namespace

const char* diagnosticSeverityName(DiagnosticSeverity severity) {
  switch (severity) {
    case DiagnosticSeverity::kInfo:
      return "info";
    case DiagnosticSeverity::kWarning:
      return "warning";
    case DiagnosticSeverity::kError:
      return "error";
  }
  return "unknown";
}

std::size_t expressionNodeCount(const Expression& expression) {
  std::size_t count = 1;
  if (expression.left) {
    count += expressionNodeCount(*expression.left);
  }
  if (expression.right) {
    count += expressionNodeCount(*expression.right);
  }
  return count;
}

void collectConfigEntryStats(const ConfigEntry& entry, const std::string& parent,
                             SchemaProfile& profile, SchemaOptions options) {
  const std::string full_key = parent.empty() ? entry.key : parent + "." + entry.key;
  const std::string normalized = normalizeIdentifier(full_key);
  auto& stats = profile.config_keys[normalized];

  if (full_key.size() > options.max_config_key_length) {
    addDiagnostic(profile, DiagnosticSeverity::kWarning, "config",
                  "configuration key is unusually long");
  }

  if (entry.is_block) {
    ++stats.blocks;
    if (entry.children.empty()) {
      addDiagnostic(profile, DiagnosticSeverity::kInfo, "config",
                    "configuration block has no children");
    }
    for (const auto& child : entry.children) {
      collectConfigEntryStats(child, full_key, profile, options);
    }
    return;
  }

  ++stats.assignments;
  const std::size_t nodes = expressionNodeCount(entry.value);
  stats.expression_nodes += nodes;
  if (nodes > 32) {
    addDiagnostic(profile, DiagnosticSeverity::kWarning, "config",
                  "configuration expression is unusually complex");
  }
  if (entry.value.kind == ExprKind::kString || entry.value.kind == ExprKind::kIdentifier) {
    stats.literal_values.insert(entry.value.text);
  }
  if (entry.value.kind == ExprKind::kNumber && !std::isfinite(entry.value.number)) {
    addDiagnostic(profile, DiagnosticSeverity::kWarning, "config",
                  "configuration number is not finite");
  }
}

Result<SchemaProfile> buildSchemaProfile(const Bundle& bundle, SchemaOptions options) {
  auto valid = validateBundle(bundle);
  if (!valid) {
    return Result<SchemaProfile>::failure(valid.error());
  }

  SchemaProfile profile;
  inspectMetadata(bundle, profile);
  inspectSectionInventory(bundle, profile, options);

  for (const auto& section : bundle.sections) {
    switch (section.type) {
      case SectionType::kMetadata:
      case SectionType::kNotes:
        break;
      case SectionType::kConfig:
        inspectConfigSection(section, profile, options);
        break;
      case SectionType::kJournal:
        inspectJournalSection(section, profile);
        break;
      case SectionType::kStream:
        inspectStreamSection(section, profile, options);
        break;
      case SectionType::kUnknown:
        addDiagnostic(profile, DiagnosticSeverity::kError, "bundle",
                      "unknown section cannot be profiled");
        break;
    }
  }

  updateSignatureFromSections(profile);
  return Result<SchemaProfile>::success(std::move(profile));
}

Result<SchemaProfile> profileBundleBytes(const std::uint8_t* data, std::size_t size,
                                         SchemaOptions options) {
  auto bundle = parseBundle(data, size);
  if (!bundle) {
    return Result<SchemaProfile>::failure(bundle.error());
  }
  return buildSchemaProfile(bundle.value(), options);
}

Result<void> validateProfile(const SchemaProfile& profile) {
  for (const auto& diagnostic : profile.diagnostics) {
    if (diagnostic.severity == DiagnosticSeverity::kError) {
      return Result<void>::failure(makeError(ErrorCode::kInvalidState,
                                            "schema profile contains an error diagnostic",
                                            diagnostic.offset));
    }
  }
  return Result<void>::success();
}

std::string formatSchemaProfile(const SchemaProfile& profile) {
  std::ostringstream out;
  out << "schema_signature=" << profile.stable_signature << '\n';
  out << "schema_sections=" << profile.sections.size() << '\n';
  out << "schema_config_keys=" << profile.config_keys.size() << '\n';
  out << "schema_keyspaces=" << profile.keyspaces.size() << '\n';
  out << "schema_stream_frames=" << profile.stream.frames << '\n';
  out << "schema_stream_messages=" << profile.stream.reconstructed_messages << '\n';
  out << "schema_stream_incomplete=" << profile.stream.incomplete_messages << '\n';
  out << "schema_diagnostics=" << profile.diagnostics.size() << '\n';
  for (const auto& diagnostic : profile.diagnostics) {
    out << "diagnostic=" << severityPrefix(diagnostic) << ":" << diagnostic.message;
    if (diagnostic.offset != 0) {
      out << "@" << diagnostic.offset;
    }
    out << '\n';
  }
  return out.str();
}

}  // namespace chronowire
