#include <gtest/gtest.h>

#include <fstream>
#include <nlohmann/json.hpp>
#include <string>

#include "subtitle_engine_c.h"

using nlohmann::json;

namespace {

std::string fixture(const std::string& name) { return std::string(SE_FIXTURES) + "/" + name; }

json take(char* s) {
  json j = json::parse(s);
  se_free(s);
  return j;
}

}  // namespace

TEST(MkvCapi, ListsAndExtracts) {
  char* error = nullptr;
  char* tracks = se_mkv_text_tracks(fixture("subs_srt.mkv").c_str(), &error);
  ASSERT_NE(tracks, nullptr);
  json t = take(tracks);
  ASSERT_EQ(t.size(), 1u);
  EXPECT_EQ(t[0]["codec"], "S_TEXT/UTF8");
  EXPECT_EQ(t[0]["language"], "eng");
  char* out = se_mkv_extract(fixture("subs_srt.mkv").c_str(), t[0]["number"].get<uint64_t>(), &error);
  ASSERT_NE(out, nullptr);
  json e = take(out);
  EXPECT_TRUE(e["complete"].get<bool>());
  ASSERT_EQ(e["cues"].size(), 5u);
  EXPECT_EQ(e["cues"][0]["start_ms"], 2000);
  EXPECT_EQ(e["cues"][0]["end_ms"], 4000);
}

TEST(MkvCapi, RejectsNonMatroska) {
  const std::string path = std::string(SE_TMP) + "/not_mkv.txt";
  std::ofstream(path) << "just some text, not a video\n";
  char* error = nullptr;
  EXPECT_EQ(se_mkv_text_tracks(path.c_str(), &error), nullptr);
  ASSERT_NE(error, nullptr);
  EXPECT_EQ(take(error)["code"], "not_matroska");
}

TEST(MkvCapi, ReportsMissingTrack) {
  char* error = nullptr;
  EXPECT_EQ(se_mkv_extract(fixture("subs_srt.mkv").c_str(), 9, &error), nullptr);
  ASSERT_NE(error, nullptr);
  EXPECT_EQ(take(error)["code"], "track_not_found");
  error = nullptr;
  EXPECT_EQ(se_mkv_extract(fixture("subs_srt.mkv").c_str(), 1, &error), nullptr);
  ASSERT_NE(error, nullptr);
  EXPECT_EQ(take(error)["code"], "not_text_track");
}

TEST(MkvCapi, ReportsMissingFile) {
  char* error = nullptr;
  EXPECT_EQ(se_mkv_text_tracks("/nope/missing.mkv", &error), nullptr);
  ASSERT_NE(error, nullptr);
  EXPECT_EQ(take(error)["code"], "io_error");
}
