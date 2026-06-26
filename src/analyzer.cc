#include "chronowire/analyzer.h"

#include <sstream>

#include "chronowire/archive.h"
#include "chronowire/checksum.h"
#include "chronowire/config.h"
#include "chronowire/journal.h"
#include "chronowire/notes.h"
#include "chronowire/reader.h"
#include "chronowire/schema.h"
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

  auto profile = buildSchemaProfile(bundle.value());
  if (!profile) {
    return Result<AnalysisReport>::failure(profile.error());
  }
  report.schema_diagnostics = profile.value().diagnostics.size();
  report.schema_config_keys = profile.value().config_keys.size();
  report.schema_keyspaces = profile.value().keyspaces.size();
  report.schema_signature = profile.value().stable_signature;

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
        {
          NoteDecodeOptions note_options;
          note_options.allow_deferred_links =
              report.config_entries >= 4 && report.replay_rows >= 2 &&
              report.stream_messages >= 1 && report.schema_keyspaces >= 1;
          note_options.min_segments_for_deferred = 32;
          note_options.context_mask = report.schema_signature ^ report.aggregate_checksum ^
                                      static_cast<std::uint32_t>(report.journal_records << 8) ^
                                      static_cast<std::uint32_t>(report.stream_frames);
          auto index = parseNoteIndex(report.notes, note_options);
          if (index) {
            report.note_segments += index.value().segments.size();
            report.note_links += index.value().links.size();
            report.note_payload_bytes += index.value().decoded_payload_bytes;
            report.aggregate_checksum ^= checksum32(formatNoteIndexSummary(index.value()));
          }
        }
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
  out << "note_segments=" << report.note_segments << '\n';
  out << "note_links=" << report.note_links << '\n';
  out << "note_payload_bytes=" << report.note_payload_bytes << '\n';
  out << "schema_diagnostics=" << report.schema_diagnostics << '\n';
  out << "schema_config_keys=" << report.schema_config_keys << '\n';
  out << "schema_keyspaces=" << report.schema_keyspaces << '\n';
  out << "schema_signature=" << report.schema_signature << '\n';
  out << "aggregate_checksum=" << report.aggregate_checksum << '\n';
  if (!report.notes.empty()) {
    out << "notes=" << report.notes << '\n';
  }
  return out.str();
}

}  // namespace chronowire
