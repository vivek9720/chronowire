#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "chronowire/result.h"

namespace chronowire {

struct AnalysisReport {
  std::size_t section_count = 0;
  std::size_t config_entries = 0;
  std::size_t include_records = 0;
  std::size_t journal_pages = 0;
  std::size_t journal_records = 0;
  std::size_t replay_rows = 0;
  std::size_t stream_frames = 0;
  std::size_t stream_messages = 0;
  std::size_t note_segments = 0;
  std::size_t note_links = 0;
  std::size_t note_payload_bytes = 0;
  std::size_t schema_diagnostics = 0;
  std::size_t schema_config_keys = 0;
  std::size_t schema_keyspaces = 0;
  std::uint32_t schema_signature = 0;
  std::uint32_t aggregate_checksum = 0;
  std::string notes;
};

Result<AnalysisReport> analyzeBundle(const std::uint8_t* data, std::size_t size);
std::string formatReport(const AnalysisReport& report);

}  // namespace chronowire
