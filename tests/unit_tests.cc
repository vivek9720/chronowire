#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

#include "chronowire/analyzer.h"
#include "chronowire/archive.h"
#include "chronowire/config.h"
#include "chronowire/journal.h"
#include "chronowire/reader.h"
#include "chronowire/schema.h"
#include "chronowire/stream.h"

namespace {

std::string completeBundle() {
  return R"(CWJ1-TEXT
version 1
meta producer=unit-test
section settings config
include "defaults.cwc";
service {
  name = "alpha";
  workers = 2 + 3 * 4;
  limits.max_rows = 128;
}
.
section store journal
JNL1
page 1 42
str 0 id
begin 7
put 7 user:1 name=ada role=admin
put 7 user:2 name=ben role=reader
commit 7
begin 8
erase 8 user:2
rollback 8
.
section link stream
STR1
frame msg=11 seq=1 total=2 flags=1 data=world
frame msg=11 seq=0 total=2 flags=0 data=hello
.
end
)";
}

void testConfig() {
  auto parsed = chronowire::parseConfig(R"(
include "common.cfg";
root {
  label = "sensor\nnode";
  size = (2 + 3) * 4;
}
)");
  assert(parsed);
  assert(parsed.value().includes.size() == 1);
  assert(chronowire::countConfigNodes(parsed.value()) == 4);
}

void testJournalReplay() {
  const std::string text = R"(JNL1
page 3 9
begin 1
put 1 key:a value=10 status=hot
commit 1
erase 0 key:a
put 0 key:b value=12
)";
  auto pages = chronowire::parseJournalText(text);
  assert(pages);
  auto replay = chronowire::replayJournal(pages.value());
  assert(replay);
  assert(replay.value().rows.count("key:a") == 0);
  assert(replay.value().rows.count("key:b") == 1);
}

void testStream() {
  const std::string text = R"(STR1
frame msg=5 seq=0 total=3 flags=0 data=ab
frame msg=5 seq=2 total=3 flags=4 data=ef
frame msg=5 seq=1 total=3 flags=0 data=cd
)";
  auto frames = chronowire::parseFramesText(text);
  assert(frames);
  auto messages = chronowire::reconstructMessages(frames.value());
  assert(messages);
  assert(messages.value().size() == 1);
  assert(chronowire::stringFromBytes(messages.value()[0].payload) == "abcdef");
}

void testBundleAnalysis() {
  const std::string text = completeBundle();
  auto report = chronowire::analyzeBundle(reinterpret_cast<const std::uint8_t*>(text.data()),
                                          text.size());
  assert(report);
  assert(report.value().section_count == 3);
  assert(report.value().include_records == 1);
  assert(report.value().journal_pages == 1);
  assert(report.value().journal_records == 7);
  assert(report.value().replay_rows == 2);
  assert(report.value().stream_frames == 2);
  assert(report.value().stream_messages == 1);
  assert(report.value().schema_config_keys >= 4);
  assert(report.value().schema_keyspaces == 1);
  assert(report.value().schema_signature != 0);
}

void testSchemaProfile() {
  const std::string text = completeBundle();
  auto profile = chronowire::profileBundleBytes(
      reinterpret_cast<const std::uint8_t*>(text.data()), text.size());
  assert(profile);
  assert(profile.value().sections.size() == 3);
  assert(profile.value().keyspaces.count("user") == 1);
  assert(profile.value().stream.reconstructed_messages == 1);
  assert(profile.value().begin_records == 2);
  assert(profile.value().commit_records == 1);
  assert(profile.value().rollback_records == 1);
  auto valid = chronowire::validateProfile(profile.value());
  assert(valid);
  const std::string formatted = chronowire::formatSchemaProfile(profile.value());
  assert(formatted.find("schema_signature=") != std::string::npos);
}

}  // namespace

int main() {
  testConfig();
  testJournalReplay();
  testStream();
  testBundleAnalysis();
  testSchemaProfile();
  return 0;
}
