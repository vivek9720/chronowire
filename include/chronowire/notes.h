#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "chronowire/result.h"

namespace chronowire {

struct NoteSegment {
  std::string name;
  std::string type;
  std::uint64_t offset = 0;
  std::uint64_t length = 0;
  std::vector<std::uint8_t> payload;
  std::uint32_t digest = 0;
};

struct NoteLink {
  std::string source;
  std::string target;
  std::string mode;
  std::uint32_t weight = 0;
  std::uint32_t combined_digest = 0;
};

struct NoteIndex {
  std::vector<NoteSegment> segments;
  std::vector<NoteLink> links;
  std::size_t decoded_payload_bytes = 0;
  std::size_t resolved_links = 0;
};

struct NoteDecodeOptions {
  bool allow_deferred_links = false;
  std::size_t min_segments_for_deferred = 16;
  std::uint32_t context_mask = 0;
};

Result<NoteIndex> parseNoteIndex(const std::string& text,
                                 NoteDecodeOptions options = NoteDecodeOptions{});
std::string formatNoteIndexSummary(const NoteIndex& index);

}  // namespace chronowire
