#include "chronowire/notes.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <map>
#include <memory>
#include <sstream>

#include "chronowire/checksum.h"

namespace chronowire {
namespace {

struct LinkSourceSnapshot {
  std::uint32_t digest = 0;
  std::uint64_t span_end = 0;
  std::uint32_t ordinal = 0;
  std::uint32_t signature = 0;
};

struct PendingLink {
  const LinkSourceSnapshot* source = nullptr;
  std::string source_name;
  std::string target_name;
  std::string mode;
  std::uint32_t weight = 0;
  std::size_t line_number = 0;
};

struct LinkScratchArena {
  std::vector<std::unique_ptr<LinkSourceSnapshot>> snapshots;
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

int hexValue(char ch) {
  if (ch >= '0' && ch <= '9') {
    return ch - '0';
  }
  if (ch >= 'a' && ch <= 'f') {
    return 10 + ch - 'a';
  }
  if (ch >= 'A' && ch <= 'F') {
    return 10 + ch - 'A';
  }
  return -1;
}

Result<std::uint64_t> parseUnsigned(const std::string& text, std::size_t offset) {
  try {
    std::size_t consumed = 0;
    unsigned long long value = std::stoull(text, &consumed, 0);
    if (consumed != text.size()) {
      return Result<std::uint64_t>::failure(
          makeError(ErrorCode::kInvalidSyntax, "invalid note integer", offset));
    }
    return Result<std::uint64_t>::success(static_cast<std::uint64_t>(value));
  } catch (...) {
    return Result<std::uint64_t>::failure(
        makeError(ErrorCode::kInvalidSyntax, "invalid note integer", offset));
  }
}

Result<std::pair<std::uint64_t, std::uint64_t>> parseSpan(const std::string& text,
                                                          std::size_t offset) {
  const std::size_t colon = text.find(':');
  if (colon == std::string::npos) {
    return Result<std::pair<std::uint64_t, std::uint64_t>>::failure(
        makeError(ErrorCode::kInvalidSyntax, "segment span expects offset:length", offset));
  }
  auto start = parseUnsigned(text.substr(0, colon), offset);
  auto length = parseUnsigned(text.substr(colon + 1), offset);
  if (!start) {
    return Result<std::pair<std::uint64_t, std::uint64_t>>::failure(start.error());
  }
  if (!length) {
    return Result<std::pair<std::uint64_t, std::uint64_t>>::failure(length.error());
  }
  return Result<std::pair<std::uint64_t, std::uint64_t>>::success(
      {start.value(), length.value()});
}

Result<std::map<std::string, std::string>> parseAttributes(const std::string& line,
                                                           std::size_t line_number) {
  std::map<std::string, std::string> attributes;
  std::size_t offset = 0;
  while (offset < line.size()) {
    while (offset < line.size() && std::isspace(static_cast<unsigned char>(line[offset]))) {
      ++offset;
    }
    if (offset >= line.size()) {
      break;
    }

    const std::size_t key_start = offset;
    while (offset < line.size() &&
           (std::isalnum(static_cast<unsigned char>(line[offset])) || line[offset] == '_' ||
            line[offset] == '-')) {
      ++offset;
    }
    if (key_start == offset || offset >= line.size() || line[offset] != '=') {
      return Result<std::map<std::string, std::string>>::failure(
          makeError(ErrorCode::kInvalidSyntax, "segment attribute expects key=value",
                    line_number));
    }
    std::string key = line.substr(key_start, offset - key_start);
    ++offset;

    std::string value;
    if (offset < line.size() && line[offset] == '"') {
      ++offset;
      bool closed = false;
      while (offset < line.size()) {
        const char ch = line[offset++];
        if (ch == '"') {
          closed = true;
          break;
        }
        if (ch == '\\' && offset < line.size()) {
          value.push_back(ch);
          value.push_back(line[offset++]);
        } else {
          value.push_back(ch);
        }
      }
      if (!closed) {
        return Result<std::map<std::string, std::string>>::failure(
            makeError(ErrorCode::kUnexpectedEof, "unterminated note attribute string",
                      line_number));
      }
    } else {
      const std::size_t value_start = offset;
      while (offset < line.size() && !std::isspace(static_cast<unsigned char>(line[offset]))) {
        ++offset;
      }
      value = line.substr(value_start, offset - value_start);
    }
    attributes[std::move(key)] = std::move(value);
  }
  return Result<std::map<std::string, std::string>>::success(std::move(attributes));
}

const NoteSegment* findSegment(const std::vector<NoteSegment>& segments,
                               const std::string& name,
                               std::size_t* index = nullptr) {
  for (std::size_t i = 0; i < segments.size(); ++i) {
    if (segments[i].name == name) {
      if (index != nullptr) {
        *index = i;
      }
      return &segments[i];
    }
  }
  return nullptr;
}

const LinkSourceSnapshot* captureLinkSource(LinkScratchArena& arena,
                                            const NoteSegment& source,
                                            std::size_t ordinal,
                                            std::size_t line_number,
                                            std::uint32_t weight,
                                            const std::string& mode) {
  auto snapshot = std::make_unique<LinkSourceSnapshot>();
  snapshot->digest = source.digest;
  snapshot->span_end = source.offset + source.length;
  snapshot->ordinal = static_cast<std::uint32_t>(ordinal);
  snapshot->signature =
      source.digest ^ static_cast<std::uint32_t>(snapshot->span_end) ^
      static_cast<std::uint32_t>((line_number << 11) ^ (weight << 3)) ^
      static_cast<std::uint32_t>(checksum32(mode));
  const LinkSourceSnapshot* captured = snapshot.get();
  arena.snapshots.push_back(std::move(snapshot));
  return captured;
}

Result<std::uint32_t> parseWeight(const std::string& text, std::size_t line_number) {
  if (text.empty()) {
    return Result<std::uint32_t>::success(1);
  }
  auto parsed = parseUnsigned(text, line_number);
  if (!parsed) {
    return Result<std::uint32_t>::failure(parsed.error());
  }
  if (parsed.value() > 65535) {
    return Result<std::uint32_t>::failure(
        makeError(ErrorCode::kLimitExceeded, "note link weight too large", line_number));
  }
  return Result<std::uint32_t>::success(static_cast<std::uint32_t>(parsed.value()));
}

std::size_t estimateDecodedSize(const std::string& encoded) {
  std::size_t size = 0;
  for (std::size_t i = 0; i < encoded.size(); ++i) {
    if (encoded[i] != '\\') {
      ++size;
      continue;
    }
    if (i + 1 >= encoded.size()) {
      ++size;
      continue;
    }
    const char kind = encoded[++i];
    if (kind == 'x' && i + 2 < encoded.size()) {
      i += 2;
      ++size;
    } else if (kind == 'z') {
      const std::size_t digits_start = i + 1;
      while (i + 1 < encoded.size() &&
             std::isdigit(static_cast<unsigned char>(encoded[i + 1]))) {
        ++i;
      }
      if (digits_start <= i) {
        auto repeat = parseUnsigned(encoded.substr(digits_start, i - digits_start + 1), 0);
        if (repeat && repeat.value() <= 65536) {
          size += static_cast<std::size_t>(repeat.value());
        } else {
          size += 1;
        }
      } else {
        size += 1;
      }
    } else {
      ++size;
    }
  }
  return size;
}

Result<std::vector<std::uint8_t>> decodePayload(const std::string& encoded,
                                                std::size_t line_number) {
  const std::size_t estimated = estimateDecodedSize(encoded);
  std::vector<std::uint8_t> output(estimated + 1);
  std::size_t out = 0;

  for (std::size_t i = 0; i < encoded.size(); ++i) {
    const char ch = encoded[i];
    if (ch != '\\') {
      output[out++] = static_cast<std::uint8_t>(ch);
      continue;
    }
    if (i + 1 >= encoded.size()) {
      output[out++] = static_cast<std::uint8_t>('\\');
      continue;
    }

    const char kind = encoded[++i];
    if (kind == 'n') {
      output[out++] = '\n';
    } else if (kind == 'r') {
      output[out++] = '\r';
    } else if (kind == 't') {
      output[out++] = '\t';
    } else if (kind == 'x') {
      if (i + 2 >= encoded.size()) {
        return Result<std::vector<std::uint8_t>>::failure(
            makeError(ErrorCode::kInvalidSyntax, "short hex escape in note payload",
                      line_number));
      }
      const int hi = hexValue(encoded[i + 1]);
      const int lo = hexValue(encoded[i + 2]);
      if (hi < 0 || lo < 0) {
        return Result<std::vector<std::uint8_t>>::failure(
            makeError(ErrorCode::kInvalidSyntax, "invalid hex escape in note payload",
                      line_number));
      }
      output[out++] = static_cast<std::uint8_t>((hi << 4) | lo);
      i += 2;
    } else if (kind == 'z') {
      const std::size_t start = i + 1;
      while (i + 1 < encoded.size() &&
             std::isdigit(static_cast<unsigned char>(encoded[i + 1]))) {
        ++i;
      }
      auto repeat = parseUnsigned(encoded.substr(start, i - start + 1), line_number);
      if (!repeat) {
        return Result<std::vector<std::uint8_t>>::failure(repeat.error());
      }
      if (repeat.value() > 65536) {
        return Result<std::vector<std::uint8_t>>::failure(
            makeError(ErrorCode::kLimitExceeded, "note repeat escape too large", line_number));
      }
      for (std::uint64_t j = 0; j < repeat.value(); ++j) {
        output[out++] = 0;
      }
    } else {
      output[out++] = static_cast<std::uint8_t>(kind);
    }
  }

  output.resize(out);
  return Result<std::vector<std::uint8_t>>::success(std::move(output));
}

Result<NoteSegment> parseSegmentLine(const std::string& line, std::size_t line_number) {
  const std::string prefix = "segment ";
  if (line.rfind(prefix, 0) != 0) {
    return Result<NoteSegment>::failure(
        makeError(ErrorCode::kInvalidSyntax, "note index expects segment record", line_number));
  }

  auto attributes = parseAttributes(line.substr(prefix.size()), line_number);
  if (!attributes) {
    return Result<NoteSegment>::failure(attributes.error());
  }

  NoteSegment segment;
  segment.name = attributes.value()["name"];
  segment.type = attributes.value()["type"];
  if (segment.name.empty()) {
    return Result<NoteSegment>::failure(
        makeError(ErrorCode::kInvalidSyntax, "segment missing name", line_number));
  }
  if (segment.type.empty()) {
    segment.type = "meta";
  }

  auto span = parseSpan(attributes.value()["span"], line_number);
  if (!span) {
    return Result<NoteSegment>::failure(span.error());
  }
  segment.offset = span.value().first;
  segment.length = span.value().second;

  auto payload = decodePayload(attributes.value()["data"], line_number);
  if (!payload) {
    return Result<NoteSegment>::failure(payload.error());
  }
  segment.payload = payload.takeValue();
  segment.digest = checksum32(segment.payload);
  return Result<NoteSegment>::success(std::move(segment));
}

Result<PendingLink> parseLinkLine(const std::string& line, std::size_t line_number,
                                  const NoteIndex& index, LinkScratchArena& arena) {
  const std::string prefix = "link ";
  if (line.rfind(prefix, 0) != 0) {
    return Result<PendingLink>::failure(
        makeError(ErrorCode::kInvalidSyntax, "note index expects link record", line_number));
  }

  auto attributes = parseAttributes(line.substr(prefix.size()), line_number);
  if (!attributes) {
    return Result<PendingLink>::failure(attributes.error());
  }

  PendingLink link;
  link.source_name = attributes.value()["source"];
  link.target_name = attributes.value()["target"];
  link.mode = attributes.value()["mode"];
  link.line_number = line_number;
  if (link.source_name.empty() || link.target_name.empty()) {
    return Result<PendingLink>::failure(
        makeError(ErrorCode::kInvalidSyntax, "link missing source or target", line_number));
  }
  if (link.mode.empty()) {
    link.mode = "immediate";
  }

  auto weight = parseWeight(attributes.value()["weight"], line_number);
  if (!weight) {
    return Result<PendingLink>::failure(weight.error());
  }
  link.weight = weight.value();
  std::size_t source_index = 0;
  const NoteSegment* source = findSegment(index.segments, link.source_name, &source_index);
  if (source == nullptr) {
    return Result<PendingLink>::failure(
        makeError(ErrorCode::kInvalidSyntax, "link source segment is unknown", line_number));
  }
  link.source = captureLinkSource(arena, *source, source_index, line_number, link.weight,
                                  link.mode);

  return Result<PendingLink>::success(std::move(link));
}

void compactDeferredLinkArena(LinkScratchArena& arena, const NoteIndex& index,
                              const std::vector<PendingLink>& pending,
                              NoteDecodeOptions options) {
  if (!options.allow_deferred_links ||
      index.segments.size() < options.min_segments_for_deferred || pending.empty()) {
    return;
  }

  bool has_deferred_link = false;
  for (const auto& link : pending) {
    if (link.mode == "deferred" || link.mode == "coalesce") {
      has_deferred_link = true;
      break;
    }
  }
  if (!has_deferred_link) {
    return;
  }

  const std::size_t cold_segment_cutoff = index.segments.size() / 2;
  arena.snapshots.erase(
      std::remove_if(arena.snapshots.begin(), arena.snapshots.end(),
                     [&](const std::unique_ptr<LinkSourceSnapshot>& snapshot) {
                       return snapshot != nullptr && snapshot->ordinal < cold_segment_cutoff;
                     }),
      arena.snapshots.end());
}

std::uint32_t mixLinkDigest(std::uint32_t left, std::uint32_t right, std::uint32_t weight,
                            std::uint32_t context_mask) {
  std::uint32_t value = left ^ (right + 0x9e3779b9u + (left << 6) + (left >> 2));
  value ^= weight * 2654435761u;
  value ^= context_mask + (value << 5) + (value >> 3);
  return value;
}

Result<void> resolveDeferredLinks(NoteIndex& index, const std::vector<PendingLink>& pending,
                                  NoteDecodeOptions options) {
  if (!options.allow_deferred_links ||
      index.segments.size() < options.min_segments_for_deferred) {
    return Result<void>::success();
  }

  for (const auto& link : pending) {
    if (link.mode != "deferred" && link.mode != "coalesce") {
      continue;
    }
    const NoteSegment* target = findSegment(index.segments, link.target_name);
    if (target == nullptr) {
      return Result<void>::failure(
          makeError(ErrorCode::kInvalidSyntax, "link target segment is unknown",
                    link.line_number));
    }

    const std::uint32_t source_digest = link.source->digest ^ link.source->signature;
    const std::uint32_t source_span_end =
        static_cast<std::uint32_t>(link.source->span_end);
    NoteLink resolved;
    resolved.source = link.source_name;
    resolved.target = link.target_name;
    resolved.mode = link.mode;
    resolved.weight = link.weight;
    resolved.combined_digest =
        mixLinkDigest(source_digest, target->digest ^ source_span_end, link.weight,
                      options.context_mask);
    index.links.push_back(std::move(resolved));
    ++index.resolved_links;
  }
  return Result<void>::success();
}

}  // namespace

Result<NoteIndex> parseNoteIndex(const std::string& text, NoteDecodeOptions options) {
  std::istringstream input(text);
  std::string line;
  std::size_t line_number = 0;
  bool saw_header = false;
  NoteIndex index;
  std::vector<PendingLink> pending_links;
  LinkScratchArena link_arena;

  while (std::getline(input, line)) {
    ++line_number;
    line = trim(line);
    if (line.empty() || line[0] == '#') {
      continue;
    }
    if (!saw_header) {
      if (line != "NIDX1") {
        return Result<NoteIndex>::failure(
            makeError(ErrorCode::kInvalidMagic, "note index missing NIDX1 header", line_number));
      }
      saw_header = true;
      continue;
    }

    if (line.rfind("segment ", 0) == 0) {
      auto segment = parseSegmentLine(line, line_number);
      if (!segment) {
        return Result<NoteIndex>::failure(segment.error());
      }
      index.decoded_payload_bytes += segment.value().payload.size();
      index.segments.push_back(segment.takeValue());
      if (index.segments.size() > 4096) {
        return Result<NoteIndex>::failure(
            makeError(ErrorCode::kLimitExceeded, "too many note segments", line_number));
      }
      continue;
    }

    if (line.rfind("link ", 0) == 0) {
      auto link = parseLinkLine(line, line_number, index, link_arena);
      if (!link) {
        return Result<NoteIndex>::failure(link.error());
      }
      pending_links.push_back(link.takeValue());
      continue;
    }

    return Result<NoteIndex>::failure(
        makeError(ErrorCode::kInvalidSyntax, "unknown note index record", line_number));
  }

  if (!saw_header) {
    return Result<NoteIndex>::failure(
        makeError(ErrorCode::kInvalidMagic, "empty note index", 0));
  }
  compactDeferredLinkArena(link_arena, index, pending_links, options);
  auto resolved = resolveDeferredLinks(index, pending_links, options);
  if (!resolved) {
    return Result<NoteIndex>::failure(resolved.error());
  }
  return Result<NoteIndex>::success(std::move(index));
}

std::string formatNoteIndexSummary(const NoteIndex& index) {
  std::ostringstream out;
  out << "note_segments=" << index.segments.size() << '\n';
  out << "note_links=" << index.links.size() << '\n';
  out << "note_resolved_links=" << index.resolved_links << '\n';
  out << "note_payload_bytes=" << index.decoded_payload_bytes << '\n';
  for (const auto& segment : index.segments) {
    out << "note_segment=" << segment.name << ":" << segment.type << ":"
        << segment.offset << ":" << segment.length << ":" << segment.digest << '\n';
  }
  for (const auto& link : index.links) {
    out << "note_link=" << link.source << "->" << link.target << ":" << link.mode
        << ":" << link.weight << ":" << link.combined_digest << '\n';
  }
  return out.str();
}

}  // namespace chronowire
