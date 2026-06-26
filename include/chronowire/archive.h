#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "chronowire/result.h"

namespace chronowire {

enum class SectionType {
  kMetadata,
  kConfig,
  kJournal,
  kStream,
  kNotes,
  kUnknown,
};

struct Section {
  std::string name;
  SectionType type = SectionType::kUnknown;
  std::uint32_t flags = 0;
  std::uint32_t checksum = 0;
  std::vector<std::uint8_t> payload;
};

struct Bundle {
  std::uint16_t version = 1;
  std::map<std::string, std::string> metadata;
  std::vector<Section> sections;
};

struct ArchiveLimits {
  std::size_t max_sections = 64;
  std::size_t max_payload_size = 1 << 20;
  std::size_t max_text_lines = 4096;
};

const char* sectionTypeName(SectionType type);
SectionType sectionTypeFromName(const std::string& name);

Result<Bundle> parseBundle(const std::uint8_t* data, std::size_t size,
                           ArchiveLimits limits = ArchiveLimits{});
Result<Bundle> parseBinaryBundle(const std::uint8_t* data, std::size_t size,
                                 ArchiveLimits limits = ArchiveLimits{});
Result<Bundle> parseTextBundle(const std::uint8_t* data, std::size_t size,
                               ArchiveLimits limits = ArchiveLimits{});

Result<void> validateBundle(const Bundle& bundle);

}  // namespace chronowire
