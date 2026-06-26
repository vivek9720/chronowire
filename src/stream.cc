#include "chronowire/stream.h"

#include <algorithm>
#include <cctype>
#include <sstream>

#include "chronowire/checksum.h"
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
  std::istringstream stream(line);
  std::vector<std::string> words;
  std::string word;
  while (stream >> word) {
    words.push_back(word);
  }
  return words;
}

Result<std::uint64_t> parseInteger(const std::string& text, std::size_t offset) {
  try {
    std::size_t consumed = 0;
    unsigned long long value = std::stoull(text, &consumed, 0);
    if (consumed != text.size()) {
      return Result<std::uint64_t>::failure(
          makeError(ErrorCode::kInvalidSyntax, "invalid frame integer", offset));
    }
    return Result<std::uint64_t>::success(value);
  } catch (...) {
    return Result<std::uint64_t>::failure(
        makeError(ErrorCode::kInvalidSyntax, "invalid frame integer", offset));
  }
}

Result<void> parseFrameAttribute(Frame& frame, const std::string& token,
                                 std::size_t line_number) {
  const std::size_t equals = token.find('=');
  if (equals == std::string::npos) {
    return Result<void>::failure(
        makeError(ErrorCode::kInvalidSyntax, "frame attribute missing '='", line_number));
  }
  const std::string key = token.substr(0, equals);
  const std::string value = token.substr(equals + 1);
  if (key == "msg") {
    auto parsed = parseInteger(value, line_number);
    if (!parsed) {
      return Result<void>::failure(parsed.error());
    }
    frame.message_id = parsed.value();
  } else if (key == "seq") {
    auto parsed = parseInteger(value, line_number);
    if (!parsed) {
      return Result<void>::failure(parsed.error());
    }
    frame.sequence = static_cast<std::uint32_t>(parsed.value());
  } else if (key == "total") {
    auto parsed = parseInteger(value, line_number);
    if (!parsed) {
      return Result<void>::failure(parsed.error());
    }
    frame.total = static_cast<std::uint32_t>(parsed.value());
  } else if (key == "flags") {
    auto parsed = parseInteger(value, line_number);
    if (!parsed) {
      return Result<void>::failure(parsed.error());
    }
    frame.flags = static_cast<std::uint32_t>(parsed.value());
  } else if (key == "data") {
    frame.payload.assign(value.begin(), value.end());
  } else {
    return Result<void>::failure(
        makeError(ErrorCode::kInvalidSyntax, "unknown frame attribute", line_number));
  }
  return Result<void>::success();
}

}  // namespace

Result<void> StreamAssembler::accept(const Frame& frame) {
  if (frame.total == 0 || frame.total > 1024) {
    return Result<void>::failure(
        makeError(ErrorCode::kLimitExceeded, "invalid frame fragment count", frame.sequence));
  }
  if (frame.sequence >= frame.total) {
    return Result<void>::failure(
        makeError(ErrorCode::kInvalidState, "frame sequence outside total", frame.sequence));
  }
  if (frame.payload.size() > (1 << 20)) {
    return Result<void>::failure(
        makeError(ErrorCode::kLimitExceeded, "frame payload too large", frame.sequence));
  }

  auto& partial = partials_[frame.message_id];
  if (partial.total == 0) {
    partial.total = frame.total;
    partial.flags = frame.flags;
  }
  if (partial.total != frame.total) {
    return Result<void>::failure(
        makeError(ErrorCode::kInvalidState, "inconsistent frame total", frame.sequence));
  }
  partial.flags |= frame.flags;
  partial.fragments[frame.sequence] = frame.payload;

  if (partial.fragments.size() == partial.total) {
    Message message;
    message.message_id = frame.message_id;
    message.flags = partial.flags;
    for (std::uint32_t seq = 0; seq < partial.total; ++seq) {
      auto found = partial.fragments.find(seq);
      if (found == partial.fragments.end()) {
        return Result<void>::success();
      }
      message.payload.insert(message.payload.end(), found->second.begin(), found->second.end());
    }
    message.rolling_checksum = checksum32(message.payload);
    completed_.push_back(std::move(message));
    partials_.erase(frame.message_id);
  }

  return Result<void>::success();
}

std::vector<Message> StreamAssembler::takeCompleted() {
  std::vector<Message> messages;
  messages.swap(completed_);
  return messages;
}

Result<std::vector<Frame>> parseFrames(const std::uint8_t* data, std::size_t size) {
  if (data != nullptr && size >= 4 &&
      std::equal(data, data + 4, reinterpret_cast<const std::uint8_t*>("FRM1"))) {
    return parseFramesBinary(data, size);
  }
  std::string text;
  if (data != nullptr && size != 0) {
    text.assign(reinterpret_cast<const char*>(data), size);
  }
  return parseFramesText(text);
}

Result<std::vector<Frame>> parseFramesText(const std::string& text) {
  std::istringstream input(text);
  std::string line;
  std::size_t line_number = 0;
  std::vector<Frame> frames;

  while (std::getline(input, line)) {
    ++line_number;
    line = trim(line);
    if (line.empty() || line[0] == '#') {
      continue;
    }
    if (line == "STR1") {
      continue;
    }
    auto words = splitWords(line);
    if (words.empty()) {
      continue;
    }
    if (words[0] != "frame") {
      return Result<std::vector<Frame>>::failure(
          makeError(ErrorCode::kInvalidSyntax, "expected frame record", line_number));
    }
    Frame frame;
    bool saw_data = false;
    for (std::size_t i = 1; i < words.size(); ++i) {
      if (words[i].rfind("data=", 0) == 0) {
        saw_data = true;
      }
      auto parsed = parseFrameAttribute(frame, words[i], line_number);
      if (!parsed) {
        return Result<std::vector<Frame>>::failure(parsed.error());
      }
    }
    if (!saw_data) {
      return Result<std::vector<Frame>>::failure(
          makeError(ErrorCode::kInvalidSyntax, "frame missing data", line_number));
    }
    frames.push_back(std::move(frame));
  }

  return Result<std::vector<Frame>>::success(std::move(frames));
}

Result<std::vector<Frame>> parseFramesBinary(const std::uint8_t* data, std::size_t size) {
  ByteReader reader(data, size);
  if (!reader.startsWith("FRM1", 4)) {
    return Result<std::vector<Frame>>::failure(
        makeError(ErrorCode::kInvalidMagic, "frame stream missing FRM1", 0));
  }
  auto skipped = reader.skip(4);
  if (!skipped) {
    return Result<std::vector<Frame>>::failure(skipped.error());
  }

  auto count = reader.readVarint(4096);
  if (!count) {
    return Result<std::vector<Frame>>::failure(count.error());
  }

  std::vector<Frame> frames;
  for (std::size_t i = 0; i < count.value(); ++i) {
    Frame frame;
    auto message_id = reader.readVarint(1ull << 40);
    auto sequence = reader.readVarint(1024);
    auto total = reader.readVarint(1024);
    auto flags = reader.readVarint(0xffff);
    auto payload_size = reader.readVarint(1 << 20);
    if (!message_id) {
      return Result<std::vector<Frame>>::failure(message_id.error());
    }
    if (!sequence) {
      return Result<std::vector<Frame>>::failure(sequence.error());
    }
    if (!total) {
      return Result<std::vector<Frame>>::failure(total.error());
    }
    if (!flags) {
      return Result<std::vector<Frame>>::failure(flags.error());
    }
    if (!payload_size) {
      return Result<std::vector<Frame>>::failure(payload_size.error());
    }
    auto payload = reader.readBytes(static_cast<std::size_t>(payload_size.value()));
    if (!payload) {
      return Result<std::vector<Frame>>::failure(payload.error());
    }
    frame.message_id = message_id.value();
    frame.sequence = static_cast<std::uint32_t>(sequence.value());
    frame.total = static_cast<std::uint32_t>(total.value());
    frame.flags = static_cast<std::uint32_t>(flags.value());
    frame.payload = payload.takeValue();
    frames.push_back(std::move(frame));
  }

  return Result<std::vector<Frame>>::success(std::move(frames));
}

Result<std::vector<Message>> reconstructMessages(const std::vector<Frame>& frames) {
  StreamAssembler assembler;
  std::vector<Message> messages;
  for (const auto& frame : frames) {
    auto accepted = assembler.accept(frame);
    if (!accepted) {
      return Result<std::vector<Message>>::failure(accepted.error());
    }
    auto completed = assembler.takeCompleted();
    messages.insert(messages.end(), completed.begin(), completed.end());
  }
  return Result<std::vector<Message>>::success(std::move(messages));
}

}  // namespace chronowire
