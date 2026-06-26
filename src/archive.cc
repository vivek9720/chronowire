#include "chronowire/archive.h"

#include <algorithm>
#include <cctype>
#include <sstream>

#include "chronowire/checksum.h"
#include "chronowire/reader.h"

namespace chronowire {
namespace {

struct BinarySectionHeader {
  std::string name;
  SectionType type = SectionType::kUnknown;
  std::uint32_t flags = 0;
  std::uint32_t length = 0;
  std::uint32_t checksum = 0;
};

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
  std::istringstream stream(line);
  std::vector<std::string> words;
  std::string word;
  while (stream >> word) {
    words.push_back(word);
  }
  return words;
}

Result<std::uint32_t> parseU32(const std::string& text, std::size_t line_number) {
  try {
    std::size_t consumed = 0;
    unsigned long value = std::stoul(text, &consumed, 0);
    if (consumed != text.size() || value > 0xffffffffUL) {
      return Result<std::uint32_t>::failure(
          makeError(ErrorCode::kInvalidSyntax, "invalid uint32", line_number));
    }
    return Result<std::uint32_t>::success(static_cast<std::uint32_t>(value));
  } catch (...) {
    return Result<std::uint32_t>::failure(
        makeError(ErrorCode::kInvalidSyntax, "invalid uint32", line_number));
  }
}

Result<void> parseMetadataLine(Bundle& bundle, const std::string& line,
                               std::size_t line_number) {
  const std::size_t equals = line.find('=');
  if (equals == std::string::npos) {
    return Result<void>::failure(
        makeError(ErrorCode::kInvalidSyntax, "metadata line missing '='", line_number));
  }

  std::string key = trim(line.substr(0, equals));
  std::string value = trim(line.substr(equals + 1));
  if (key.empty()) {
    return Result<void>::failure(
        makeError(ErrorCode::kInvalidSyntax, "empty metadata key", line_number));
  }
  bundle.metadata[key] = value;
  return Result<void>::success();
}

}  // namespace

const char* sectionTypeName(SectionType type) {
  switch (type) {
    case SectionType::kMetadata:
      return "metadata";
    case SectionType::kConfig:
      return "config";
    case SectionType::kJournal:
      return "journal";
    case SectionType::kStream:
      return "stream";
    case SectionType::kNotes:
      return "notes";
    case SectionType::kUnknown:
      return "unknown";
  }
  return "unknown";
}

SectionType sectionTypeFromName(const std::string& name) {
  std::string lowered = name;
  std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  if (lowered == "metadata" || lowered == "meta") {
    return SectionType::kMetadata;
  }
  if (lowered == "config" || lowered == "cfg") {
    return SectionType::kConfig;
  }
  if (lowered == "journal" || lowered == "pages") {
    return SectionType::kJournal;
  }
  if (lowered == "stream" || lowered == "frames") {
    return SectionType::kStream;
  }
  if (lowered == "notes") {
    return SectionType::kNotes;
  }
  return SectionType::kUnknown;
}

Result<Bundle> parseBundle(const std::uint8_t* data, std::size_t size,
                           ArchiveLimits limits) {
  if (data != nullptr && size >= 4 &&
      std::equal(data, data + 4, reinterpret_cast<const std::uint8_t*>("CWJB"))) {
    return parseBinaryBundle(data, size, limits);
  }
  return parseTextBundle(data, size, limits);
}

Result<Bundle> parseBinaryBundle(const std::uint8_t* data, std::size_t size,
                                 ArchiveLimits limits) {
  ByteReader reader(data, size);
  if (!reader.startsWith("CWJB", 4)) {
    return Result<Bundle>::failure(
        makeError(ErrorCode::kInvalidMagic, "binary bundle missing CWJB magic", 0));
  }
  auto magic = reader.skip(4);
  if (!magic) {
    return Result<Bundle>::failure(magic.error());
  }

  auto version = reader.readLe16();
  if (!version) {
    return Result<Bundle>::failure(version.error());
  }
  if (version.value() == 0 || version.value() > 2) {
    return Result<Bundle>::failure(
        makeError(ErrorCode::kUnsupportedVersion, "unsupported bundle version", 4));
  }

  auto section_count = reader.readLe16();
  if (!section_count) {
    return Result<Bundle>::failure(section_count.error());
  }
  if (section_count.value() > limits.max_sections) {
    return Result<Bundle>::failure(
        makeError(ErrorCode::kLimitExceeded, "too many bundle sections", reader.offset()));
  }

  std::vector<BinarySectionHeader> headers;
  headers.reserve(section_count.value());
  for (std::size_t i = 0; i < section_count.value(); ++i) {
    auto name_len = reader.readU8();
    if (!name_len) {
      return Result<Bundle>::failure(name_len.error());
    }
    auto name = reader.readString(name_len.value());
    if (!name) {
      return Result<Bundle>::failure(name.error());
    }
    auto type = reader.readU8();
    auto flags = reader.readLe16();
    auto length = reader.readLe32();
    auto expected_checksum = reader.readLe32();
    if (!type) {
      return Result<Bundle>::failure(type.error());
    }
    if (!flags) {
      return Result<Bundle>::failure(flags.error());
    }
    if (!length) {
      return Result<Bundle>::failure(length.error());
    }
    if (!expected_checksum) {
      return Result<Bundle>::failure(expected_checksum.error());
    }
    if (length.value() > limits.max_payload_size) {
      return Result<Bundle>::failure(
          makeError(ErrorCode::kLimitExceeded, "section payload too large", reader.offset()));
    }

    SectionType section_type = SectionType::kUnknown;
    switch (type.value()) {
      case 1:
        section_type = SectionType::kMetadata;
        break;
      case 2:
        section_type = SectionType::kConfig;
        break;
      case 3:
        section_type = SectionType::kJournal;
        break;
      case 4:
        section_type = SectionType::kStream;
        break;
      case 5:
        section_type = SectionType::kNotes;
        break;
      default:
        section_type = SectionType::kUnknown;
        break;
    }

    headers.push_back(BinarySectionHeader{name.value(), section_type, flags.value(),
                                          length.value(), expected_checksum.value()});
  }

  Bundle bundle;
  bundle.version = version.value();
  for (const auto& header : headers) {
    auto payload = reader.readBytes(header.length);
    if (!payload) {
      return Result<Bundle>::failure(payload.error());
    }
    if (header.checksum != 0 && checksum32(payload.value()) != header.checksum) {
      return Result<Bundle>::failure(
          makeError(ErrorCode::kChecksumMismatch, "section checksum mismatch", reader.offset()));
    }
    Section section;
    section.name = header.name;
    section.type = header.type;
    section.flags = header.flags;
    section.checksum = header.checksum;
    section.payload = payload.takeValue();
    bundle.sections.push_back(std::move(section));
  }

  return Result<Bundle>::success(std::move(bundle));
}

Result<Bundle> parseTextBundle(const std::uint8_t* data, std::size_t size,
                               ArchiveLimits limits) {
  std::string text;
  if (data != nullptr && size != 0) {
    text.assign(reinterpret_cast<const char*>(data), size);
  }
  std::istringstream input(text);
  std::string line;
  std::size_t line_number = 0;

  if (!std::getline(input, line)) {
    return Result<Bundle>::failure(
        makeError(ErrorCode::kUnexpectedEof, "empty bundle input", 0));
  }
  ++line_number;
  if (trim(line) != "CWJ1-TEXT") {
    return Result<Bundle>::failure(
        makeError(ErrorCode::kInvalidMagic, "text bundle missing CWJ1-TEXT header", 0));
  }

  Bundle bundle;
  while (std::getline(input, line)) {
    ++line_number;
    if (line_number > limits.max_text_lines) {
      return Result<Bundle>::failure(
          makeError(ErrorCode::kLimitExceeded, "too many text bundle lines", line_number));
    }

    line = trim(line);
    if (line.empty() || line[0] == '#') {
      continue;
    }
    if (line == "end") {
      break;
    }
    if (line.rfind("version ", 0) == 0) {
      auto parsed = parseU32(trim(line.substr(8)), line_number);
      if (!parsed) {
        return Result<Bundle>::failure(parsed.error());
      }
      if (parsed.value() == 0 || parsed.value() > 2) {
        return Result<Bundle>::failure(
            makeError(ErrorCode::kUnsupportedVersion, "unsupported text bundle version",
                      line_number));
      }
      bundle.version = static_cast<std::uint16_t>(parsed.value());
      continue;
    }
    if (line.rfind("meta ", 0) == 0) {
      auto parsed = parseMetadataLine(bundle, trim(line.substr(5)), line_number);
      if (!parsed) {
        return Result<Bundle>::failure(parsed.error());
      }
      continue;
    }
    if (line.rfind("section ", 0) == 0) {
      auto words = splitWords(line);
      if (words.size() < 3 || words.size() > 4) {
        return Result<Bundle>::failure(
            makeError(ErrorCode::kInvalidSyntax, "section header expects name, type, checksum",
                      line_number));
      }
      if (bundle.sections.size() >= limits.max_sections) {
        return Result<Bundle>::failure(
            makeError(ErrorCode::kLimitExceeded, "too many bundle sections", line_number));
      }

      Section section;
      section.name = words[1];
      section.type = sectionTypeFromName(words[2]);
      if (words.size() == 4) {
        auto parsed_checksum = parseU32(words[3], line_number);
        if (!parsed_checksum) {
          return Result<Bundle>::failure(parsed_checksum.error());
        }
        section.checksum = parsed_checksum.value();
      }

      std::ostringstream payload;
      bool closed = false;
      while (std::getline(input, line)) {
        ++line_number;
        if (trim(line) == ".") {
          closed = true;
          break;
        }
        payload << line << '\n';
        if (static_cast<std::size_t>(payload.tellp()) > limits.max_payload_size) {
          return Result<Bundle>::failure(
              makeError(ErrorCode::kLimitExceeded, "text section payload too large",
                        line_number));
        }
      }
      if (!closed) {
        return Result<Bundle>::failure(
            makeError(ErrorCode::kUnexpectedEof, "unterminated text section", line_number));
      }

      std::string payload_text = payload.str();
      section.payload = bytesFromString(payload_text);
      if (section.checksum != 0 && checksum32(section.payload) != section.checksum) {
        return Result<Bundle>::failure(
            makeError(ErrorCode::kChecksumMismatch, "text section checksum mismatch",
                      line_number));
      }
      bundle.sections.push_back(std::move(section));
      continue;
    }

    return Result<Bundle>::failure(
        makeError(ErrorCode::kInvalidSyntax, "unknown bundle directive", line_number));
  }

  return Result<Bundle>::success(std::move(bundle));
}

Result<void> validateBundle(const Bundle& bundle) {
  if (bundle.version == 0 || bundle.version > 2) {
    return Result<void>::failure(
        makeError(ErrorCode::kUnsupportedVersion, "unsupported bundle version", 0));
  }
  if (bundle.sections.empty()) {
    return Result<void>::failure(
        makeError(ErrorCode::kInvalidState, "bundle contains no sections", 0));
  }
  for (const auto& section : bundle.sections) {
    if (section.name.empty()) {
      return Result<void>::failure(
          makeError(ErrorCode::kInvalidState, "section has empty name", 0));
    }
    if (section.type == SectionType::kUnknown) {
      return Result<void>::failure(
          makeError(ErrorCode::kUnknownSection, "section has unknown type", 0));
    }
  }
  return Result<void>::success();
}

}  // namespace chronowire
