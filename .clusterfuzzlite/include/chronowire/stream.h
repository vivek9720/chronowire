#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "chronowire/result.h"

namespace chronowire {

struct Frame {
  std::uint64_t message_id = 0;
  std::uint32_t sequence = 0;
  std::uint32_t total = 1;
  std::uint32_t flags = 0;
  std::vector<std::uint8_t> payload;
};

struct Message {
  std::uint64_t message_id = 0;
  std::uint32_t flags = 0;
  std::vector<std::uint8_t> payload;
  std::uint32_t rolling_checksum = 0;
};

class StreamAssembler {
 public:
  Result<void> accept(const Frame& frame);
  std::vector<Message> takeCompleted();
  std::size_t pendingCount() const { return partials_.size(); }

 private:
  struct Partial {
    std::uint32_t total = 0;
    std::uint32_t flags = 0;
    std::map<std::uint32_t, std::vector<std::uint8_t>> fragments;
  };

  std::map<std::uint64_t, Partial> partials_;
  std::vector<Message> completed_;
};

Result<std::vector<Frame>> parseFrames(const std::uint8_t* data, std::size_t size);
Result<std::vector<Frame>> parseFramesText(const std::string& text);
Result<std::vector<Frame>> parseFramesBinary(const std::uint8_t* data, std::size_t size);
Result<std::vector<Message>> reconstructMessages(const std::vector<Frame>& frames);

}  // namespace chronowire
