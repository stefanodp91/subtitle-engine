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
