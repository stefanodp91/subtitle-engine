#include <gtest/gtest.h>
#include <sys/stat.h>

#include <chrono>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

#include "fake_recognizer.hpp"
#include "file_source.hpp"
#include "subtitle_engine/subtitle_store.hpp"
#include "subtitle_engine/subtitler.hpp"

using namespace subtitle_engine;
using namespace std::chrono_literals;

namespace {

std::string fixture(const std::string& name) { return std::string(SE_FIXTURES) + "/" + name; }

// A fresh folder for one test's file.
std::string folder(const std::string& name) {
  const std::string dir = std::string(SE_TMP) + "/store_" + name;
  ::chmod(dir.c_str(), 0755);
  std::remove((dir + "/subtitles.json").c_str());
  std::remove((dir + "/subtitles.json.part").c_str());
  ::mkdir(dir.c_str(), 0755);
  return dir;
}

std::string readAll(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), {});
}

bool exists(const std::string& path) { return std::ifstream(path).good(); }

StoredSubtitles sample(const std::string& media = "film-v1") {
  StoredSubtitles s;
  s.mediaId = media;
  s.targetLanguage = "it";
  s.audioLanguage = "en";
  s.ranges.add(0, 8000);
  s.cues = {{1000, 3000, "Ciao."}, {4000, 6500, "Una riga\nsu due righe."}};
  return s;
}

SubtitlerSpec storeSpec(const std::string& path) {
  SubtitlerSpec s;
  s.targetLanguage = "it";
  s.storePath = path;
  s.mediaId = "film-v1";
  s.config.waitPoll = 20ms;
  return s;
}

std::unique_ptr<Subtitler> openWith(FakeRecognizer& rec, MediaSource& source, const SubtitlerSpec& s) {
  std::string error;
  auto sub = Subtitler::open(rec, source, s, nullptr, &error);
  EXPECT_TRUE(sub) << error;
  return sub;
}

}  // namespace

TEST(SubtitleStore, RoundTrips) {
  const std::string path = folder("roundtrip") + "/subtitles.json";
  SubtitleStore store(path);
  std::string error;
  ASSERT_TRUE(store.save(sample(), &error)) << error;
  auto back = store.load("film-v1", "it", &error);
  ASSERT_TRUE(back) << error;
  EXPECT_EQ(back->audioLanguage, "en");
  EXPECT_EQ(back->ranges.spans(), sample().ranges.spans());
  ASSERT_EQ(back->cues.size(), 2u);
  EXPECT_EQ(back->cues[1].text, "Una riga\nsu due righe.");
  EXPECT_EQ(back->cues[1].startMs, 4000);
}

TEST(SubtitleStore, CorruptFileStartsOver) {
  const std::string path = folder("corrupt") + "/subtitles.json";
  SubtitleStore store(path);
  std::string error;
  ASSERT_TRUE(store.save(sample(), &error));
  const std::string whole = readAll(path);
  std::ofstream(path, std::ios::binary | std::ios::trunc) << whole.substr(0, whole.size() / 2);
  EXPECT_FALSE(store.load("film-v1", "it", &error));
  EXPECT_EQ(error, "corrupt_data");

  FakeRecognizer rec;
  rec.cues = {{2000, 4000, "Hello there."}};
  FakeTranslator translator;
  FileSource source(fixture("audio_long.mkv"));
  auto sub = openWith(rec, source, storeSpec(path));
  sub->setTranslator(&translator);
  Step s = sub->step();
  EXPECT_EQ(s.fromMs, 0);
  auto again = store.load("film-v1", "it", &error);
  ASSERT_TRUE(again) << error;
  EXPECT_EQ(again->cues.front().text, "IT:Hello there.");
}

TEST(SubtitleStore, OtherMediaStartsOver) {
  const std::string path = folder("other") + "/subtitles.json";
  SubtitleStore store(path);
  std::string error;
  ASSERT_TRUE(store.save(sample("film-v1"), &error));
  EXPECT_FALSE(store.load("film-v2", "it", &error));
  EXPECT_EQ(error, "other_media");
}

TEST(SubtitleStore, FailedSaveKeepsOldFile) {
  const std::string dir = folder("readonly");
  const std::string path = dir + "/subtitles.json";
  SubtitleStore store(path);
  std::string error;
  ASSERT_TRUE(store.save(sample(), &error));
  const std::string before = readAll(path);
  ::chmod(dir.c_str(), 0555);
  StoredSubtitles changed = sample();
  changed.cues.push_back({9000, 11000, "Altro."});
  EXPECT_FALSE(store.save(changed, &error));
  EXPECT_EQ(error, "store_failed");
  ::chmod(dir.c_str(), 0755);
  EXPECT_EQ(readAll(path), before);
  EXPECT_FALSE(exists(path + ".part"));
}

TEST(Subtitler, ResumesFromSavedRange) {
  const std::string path = folder("resume") + "/subtitles.json";
  FakeTranslator translator;
  int64_t savedUntil = 0;
  {
    FakeRecognizer rec;
    rec.cues = {{2000, 4000, "Before the break."}};
    FileSource source(fixture("audio_long.mkv"));
    auto sub = openWith(rec, source, storeSpec(path));
    sub->setTranslator(&translator);
    sub->step();
    savedUntil = sub->step().readyUntilMs;
  }
  FakeRecognizer rec;
  FileSource source(fixture("audio_long.mkv"));
  auto sub = openWith(rec, source, storeSpec(path));
  sub->setTranslator(&translator);
  sub->seek(0);
  Step s = sub->step();
  EXPECT_EQ(s.fromMs, savedUntil);
  EXPECT_LE(s.readyUntilMs - s.fromMs, 8000);  // DEC-845: short again where the saved stretch ends
  std::string error;
  auto stored = SubtitleStore(path).load("film-v1", "it", &error);
  ASSERT_TRUE(stored) << error;
  EXPECT_EQ(stored->cues.front().text, "IT:Before the break.");
}

// A film opened again where a saved stretch already covers the start point:
// the queue goes on where that stretch ends, not from the film's beginning.
TEST(Subtitler, SeekIntoSavedRangeGoesOnFromItsEnd) {
  const std::string path = folder("seek_saved") + "/subtitles.json";
  FakeTranslator translator;
  int64_t savedUntil = 0;
  {
    FakeRecognizer rec;
    FileSource source(fixture("audio_long.mkv"));
    auto sub = openWith(rec, source, storeSpec(path));
    sub->setTranslator(&translator);
    sub->seek(60000);
    sub->step();
    savedUntil = sub->step().readyUntilMs;
  }
  ASSERT_GT(savedUntil, 61000);
  FakeRecognizer rec;
  FileSource source(fixture("audio_long.mkv"));
  auto sub = openWith(rec, source, storeSpec(path));
  sub->setTranslator(&translator);
  sub->seek(savedUntil - 1000);
  Step s = sub->step();
  EXPECT_EQ(s.fromMs, savedUntil);
  EXPECT_LE(s.readyUntilMs - s.fromMs, 8000);  // short again where the saved stretch ends
}

TEST(Subtitler, SavedCuesStaySorted) {
  const std::string path = folder("sorted") + "/subtitles.json";
  FakeRecognizer rec;
  rec.cues = {{2000, 4000, "At the start."}, {62000, 64000, "Later on."}};
  FakeTranslator translator;
  FileSource source(fixture("audio_long.mkv"));
  auto sub = openWith(rec, source, storeSpec(path));
  sub->setTranslator(&translator);
  sub->seek(60000);
  sub->step();
  sub->seek(0);
  sub->step();
  std::string error;
  auto stored = SubtitleStore(path).load("film-v1", "it", &error);
  ASSERT_TRUE(stored) << error;
  EXPECT_EQ(stored->ranges.spans().size(), 2u);
  ASSERT_EQ(stored->cues.size(), 2u);
  EXPECT_LT(stored->cues[0].startMs, stored->cues[1].startMs);
  EXPECT_EQ(stored->cues[0].text, "IT:At the start.");
}

// The model that made a file: another one hears the film again from the start.
TEST(SubtitleStore, OtherModelStartsOver) {
  const std::string path = folder("model") + "/subtitles.json";
  StoredSubtitles s = sample();
  s.model = "speech@1";
  std::string error;
  ASSERT_TRUE(SubtitleStore(path).save(s, &error)) << error;
  auto same = SubtitleStore(path).load("film-v1", "it", std::string("speech@1"), &error);
  ASSERT_TRUE(same) << error;
  EXPECT_EQ(same->model, "speech@1");
  EXPECT_FALSE(SubtitleStore(path).load("film-v1", "it", std::string("speech@2"), &error));
  EXPECT_EQ(error, "other_model");

  FakeRecognizer rec;
  FakeTranslator translator;
  FileSource source(fixture("audio_long.mkv"));
  SubtitlerSpec spec = storeSpec(path);
  spec.storeModel = "speech@2";
  auto sub = openWith(rec, source, spec);
  sub->setTranslator(&translator);
  EXPECT_EQ(sub->step().fromMs, 0) << "the saved stretch of the other model is not skipped";
  auto rewritten = SubtitleStore(path).load("film-v1", "it", std::string("speech@2"), &error);
  ASSERT_TRUE(rewritten) << error;
  EXPECT_EQ(rewritten->cues.size(), 0u);
}

// A file of the first format, which said no model, is heard again.
TEST(SubtitleStore, FirstFormatStartsOver) {
  const std::string path = folder("format1") + "/subtitles.json";
  std::ofstream(path) << R"({"format":1,"media_id":"film-v1","target_language":"it","audio_language":"en",)"
                         R"("ranges":[[0,8000]],"cues":[{"start_ms":1000,"end_ms":3000,"text":"Ciao."}]})";
  std::string error;
  EXPECT_FALSE(SubtitleStore(path).load("film-v1", "it", std::string(""), &error));
  EXPECT_EQ(error, "other_format");
}

// A save cut short never says a stretch is ready: the file is read whole or not at all.
TEST(SubtitleStore, TruncatedFileGivesNothing) {
  const std::string path = folder("truncated") + "/subtitles.json";
  std::string error;
  ASSERT_TRUE(SubtitleStore(path).save(sample(), &error)) << error;
  const std::string whole = readAll(path);
  for (const size_t at : {size_t{0}, whole.find("ranges") + 9, whole.find("cues") + 12, whole.find("righe") + 2,
                          whole.size() - 1}) {
    std::ofstream(path, std::ios::binary | std::ios::trunc) << whole.substr(0, at);
    EXPECT_FALSE(SubtitleStore(path).load("film-v1", "it", &error)) << "cut at " << at;
    EXPECT_EQ(error, "corrupt_data") << "cut at " << at;
  }
}

// A part left by a save that never ended: not read, and the next save replaces it.
TEST(SubtitleStore, LeftoverPartIsIgnored) {
  const std::string path = folder("part") + "/subtitles.json";
  std::string error;
  ASSERT_TRUE(SubtitleStore(path).save(sample(), &error)) << error;
  std::ofstream(path + ".part") << "{\"format\":2,\"ranges\":[[0,99";
  auto loaded = SubtitleStore(path).load("film-v1", "it", &error);
  ASSERT_TRUE(loaded) << error;
  EXPECT_EQ(loaded->cues.size(), 2u);
  ASSERT_TRUE(SubtitleStore(path).save(sample(), &error)) << error;
  EXPECT_FALSE(exists(path + ".part"));
}

// Every cue of the file starts inside a stretch of the file: a cue pushed past
// the end of a short stretch by the one before it would be lost to a reader
// that sorts the cues by stretch.
TEST(Subtitler, CuesStayInsideTheirStretches) {
  // The film ends at 120 s: a tail of a few hundred ms after a short cue the
  // formatter keeps on screen for its minimum.
  for (int64_t from = 111000; from <= 111500; from += 100) {
    const std::string path = folder("inside") + "/subtitles.json";
    const int64_t pieceEnd = from + 8000;  // the first piece
    FakeRecognizer rec;
    rec.cues = {{pieceEnd - 100, pieceEnd - 50, "Short."}, {pieceEnd + 200, pieceEnd + 300, "Last."}};
    FakeTranslator translator;
    FileSource source(fixture("audio_long.mkv"));
    auto sub = openWith(rec, source, storeSpec(path));
    sub->setTranslator(&translator);
    sub->seek(from);
    for (int i = 0; i < 6; ++i)
      if (sub->step().status == StepStatus::kEndOfMedia) break;
    std::string error;
    auto stored = SubtitleStore(path).load("film-v1", "it", &error);
    ASSERT_TRUE(stored) << error;
    for (const Cue& c : stored->cues) {
      bool inside = false;
      for (const auto& r : stored->ranges.spans()) inside = inside || (c.startMs >= r.first && c.startMs < r.second);
      EXPECT_TRUE(inside) << "from " << from << ": " << c.text << " at " << c.startMs;
    }
  }
}

// A jump a little before a saved stretch hears up to it, not into it: no
// instant is heard twice, and the file holds each line once.
TEST(Subtitler, JumpBeforeSavedStretchHearsNothingTwice) {
  const std::string path = folder("twice") + "/subtitles.json";
  FakeTranslator translator;
  {
    FakeRecognizer rec;
    rec.cues = {{62000, 63000, "Saved line."}};
    FileSource source(fixture("audio_long.mkv"));
    auto sub = openWith(rec, source, storeSpec(path));
    sub->setTranslator(&translator);
    sub->seek(60000);
    sub->step();
    sub->step();
  }
  FakeRecognizer rec;
  rec.cues = {{56000, 57000, "Before."}, {62000, 63000, "Saved line."}};
  FileSource source(fixture("audio_long.mkv"));
  auto sub = openWith(rec, source, storeSpec(path));
  sub->setTranslator(&translator);
  sub->seek(55000);
  const Step s = sub->step();
  EXPECT_EQ(s.fromMs, 55000);
  EXPECT_LE(s.readyUntilMs, 60000);
  for (const auto& call : rec.calls()) EXPECT_LE(call.startMs + call.durationMs, 60000) << "heard from " << call.startMs;
  std::string error;
  auto stored = SubtitleStore(path).load("film-v1", "it", &error);
  ASSERT_TRUE(stored) << error;
  int saved = 0;
  for (const Cue& c : stored->cues) saved += c.text == "IT:Saved line." ? 1 : 0;
  EXPECT_EQ(saved, 1);
}
