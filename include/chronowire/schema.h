#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "chronowire/archive.h"
#include "chronowire/config.h"
#include "chronowire/result.h"

namespace chronowire {

enum class DiagnosticSeverity {
  kInfo,
  kWarning,
  kError,
};

struct Diagnostic {
  DiagnosticSeverity severity = DiagnosticSeverity::kInfo;
  std::string category;
  std::string message;
  std::size_t offset = 0;
};

struct SectionStats {
  std::string name;
  SectionType type = SectionType::kUnknown;
  std::size_t payload_size = 0;
  bool has_checksum = false;
};

struct ConfigKeyStats {
  std::size_t assignments = 0;
  std::size_t blocks = 0;
  std::size_t expression_nodes = 0;
  std::set<std::string> literal_values;
};

struct KeyspaceStats {
  std::size_t put_records = 0;
  std::size_t erase_records = 0;
  std::set<std::string> field_names;
};

struct StreamStats {
  std::size_t frames = 0;
  std::size_t reconstructed_messages = 0;
  std::size_t incomplete_messages = 0;
  std::size_t compressed_flag_mentions = 0;
  std::size_t payload_bytes = 0;
};

struct SchemaProfile {
  std::vector<SectionStats> sections;
  std::vector<Diagnostic> diagnostics;
  std::map<std::string, ConfigKeyStats> config_keys;
  std::map<std::string, KeyspaceStats> keyspaces;
  StreamStats stream;
  std::size_t begin_records = 0;
  std::size_t commit_records = 0;
  std::size_t rollback_records = 0;
  std::size_t dangling_transactions = 0;
  std::uint32_t stable_signature = 0;
};

struct SchemaOptions {
  bool require_config_section = false;
  bool require_journal_section = false;
  bool require_stream_section = false;
  std::size_t max_include_records = 64;
  std::size_t max_config_key_length = 160;
  std::size_t max_stream_fragments_per_message = 1024;
};

const char* diagnosticSeverityName(DiagnosticSeverity severity);

Result<SchemaProfile> buildSchemaProfile(const Bundle& bundle,
                                         SchemaOptions options = SchemaOptions{});
Result<SchemaProfile> profileBundleBytes(const std::uint8_t* data, std::size_t size,
                                         SchemaOptions options = SchemaOptions{});
Result<void> validateProfile(const SchemaProfile& profile);
std::string formatSchemaProfile(const SchemaProfile& profile);

std::size_t expressionNodeCount(const Expression& expression);
void collectConfigEntryStats(const ConfigEntry& entry, const std::string& parent,
                             SchemaProfile& profile, SchemaOptions options);

}  // namespace chronowire
