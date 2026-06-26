#include <cstddef>
#include <cstdint>
#include <string>

#include "chronowire/analyzer.h"
#include "chronowire/config.h"
#include "chronowire/journal.h"
#include "chronowire/stream.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  if (data == nullptr && size != 0) {
    return 0;
  }

  auto report = chronowire::analyzeBundle(data, size);
  if (report) {
    volatile std::size_t sink = report.value().section_count + report.value().config_entries +
                                report.value().journal_records + report.value().stream_messages;
    (void)sink;
  }

  if (size <= 65536) {
    std::string text;
    if (data != nullptr && size != 0) {
      text.assign(reinterpret_cast<const char*>(data), size);
    }
    auto config = chronowire::parseConfig(text);
    if (config) {
      volatile std::size_t nodes = chronowire::countConfigNodes(config.value());
      (void)nodes;
    }

    auto journal = chronowire::parseJournal(data, size);
    if (journal) {
      auto replay = chronowire::replayJournal(journal.value());
      if (replay) {
        volatile std::size_t rows = replay.value().rows.size();
        (void)rows;
      }
    }

    auto frames = chronowire::parseFrames(data, size);
    if (frames) {
      auto messages = chronowire::reconstructMessages(frames.value());
      if (messages) {
        volatile std::size_t count = messages.value().size();
        (void)count;
      }
    }
  }

  return 0;
}
