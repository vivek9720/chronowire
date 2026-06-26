#include "chronowire/analyzer.h"

#include <sstream>

#include "chronowire/archive.h"
#include "chronowire/checksum.h"
#include "chronowire/config.h"
#include "chronowire/journal.h"
#include "chronowire/reader.h"
#include "chronowire/stream.h"

namespace chronowire {

Result<AnalysisReport> analyzeBundle(const std::uint8_t* data, std::size_t size) {
  auto bundle = parseBundle(data, size);
  if (!bundle) {
    return Result<AnalysisReport>::failure(bundle.error());
  }

  auto valid = validateBundle(bundle.value());
  if (!valid) {
    return Result<AnalysisReport>::failure(valid.error());
  }

  AnalysisReport report;
  report.section_count = bundle.value().sections.size();
  report.aggregate_checksum = checksum32(data, size);

  for (const auto& section : bundle.value().sections) {
    switch (section.type) {
      case SectionType::kMetadata: {
        std::string text = stringFromBytes(section.payload);
        report.notes += "metadata:" + section.name + ";";
        if (!text.empty()) {
          report.aggregate_checksum ^= checksum32(text);
        }
        break;
      }
      case SectionType::kConfig: {
        const std::string text = stringFromBytes(section.payload);
        auto config = parseConfig(text);
        if (!config) {
          return Result<AnalysisReport>::failure(config.error());
        }
        report.config_entries += countConfigNodes(config.value());
        report.include_records += config.value().includes.size();
        break;
      }
      case SectionType::kJournal: {
        auto pages = parseJournal(section.payload.data(), section.payload.size());
        if (!pages) {
          return Result<AnalysisReport>::failure(pages.error());
        }
        report.journal_pages += pages.value().size();
        for (const auto& page : pages.value()) {
          report.journal_records += page.records.size();
        }
        auto replay = replayJournal(pages.value());
        if (!replay) {
          return Result<AnalysisReport>::failure(replay.error());
        }
        report.replay_rows += replay.value().rows.size();
        break;
      }
      case SectionType::kStream: {
        auto frames = parseFrames(section.payload.data(), section.payload.size());
        if (!frames) {
          return Result<AnalysisReport>::failure(frames.error());
        }
        report.stream_frames += frames.value().size();
        auto messages = reconstructMessages(frames.value());
        if (!messages) {
          return Result<AnalysisReport>::failure(messages.error());
        }
        report.stream_messages += messages.value().size();
        break;
      }
      case SectionType::kNotes:
        report.notes += stringFromBytes(section.payload);
        break;
      case SectionType::kUnknown:
        return Result<AnalysisReport>::failure(
            makeError(ErrorCode::kUnknownSection, "unknown section during analysis", 0));
    }
  }

  return Result<AnalysisReport>::success(std::move(report));
}

std::string formatReport(const AnalysisReport& report) {
  std::ostringstream out;
  out << "sections=" << report.section_count << '\n';
  out << "config_entries=" << report.config_entries << '\n';
  out << "include_records=" << report.include_records << '\n';
  out << "journal_pages=" << report.journal_pages << '\n';
  out << "journal_records=" << report.journal_records << '\n';
  out << "replay_rows=" << report.replay_rows << '\n';
  out << "stream_frames=" << report.stream_frames << '\n';
  out << "stream_messages=" << report.stream_messages << '\n';
  out << "aggregate_checksum=" << report.aggregate_checksum << '\n';
  if (!report.notes.empty()) {
    out << "notes=" << report.notes << '\n';
  }
  return out.str();
}

}  // namespace chronowire
