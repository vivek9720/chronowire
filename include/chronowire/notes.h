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

struct NoteIndex {
  std::vector<NoteSegment> segments;
  std::size_t decoded_payload_bytes = 0;
};

Result<NoteIndex> parseNoteIndex(const std::string& text);
std::string formatNoteIndexSummary(const NoteIndex& index);

}  // namespace chronowire
