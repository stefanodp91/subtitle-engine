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

TEST(MkvCapi, ReturnsJsonForInvalidUtf8Text) {
  // Same layout as minimalMkv in test_mkv.cpp, with a block whose text is not UTF-8.
  auto element = [](const std::string& id, const std::string& data) {
    std::string size(1, '\x01');
    for (int shift = 48; shift >= 0; shift -= 8) size += static_cast<char>((data.size() >> shift) & 0xFF);
    return id + size + data;
  };
  std::string ebml = element("\x1A\x45\xDF\xA3", element(std::string("\x42\x82", 2), "matroska"));
  std::string entry = element(std::string("\xD7", 1), std::string("\x01", 1)) +
                      element(std::string("\x83", 1), std::string("\x11", 1)) +
                      element(std::string("\x86", 1), "S_TEXT/UTF8");
  std::string tracks = element("\x16\x54\xAE\x6B", element(std::string("\xAE", 1), entry));
  std::string block = std::string("\x81\x00\x00\x00", 4) + "bad \xFF\xFE text";
  std::string cluster = element("\x1F\x43\xB6\x75", element(std::string("\xE7", 1), std::string("\x00", 1)) +
                                                        element(std::string("\xA3", 1), block));
  const std::string path = std::string(SE_TMP) + "/bad_utf8.mkv";
  std::ofstream(path, std::ios::binary) << ebml + element("\x18\x53\x80\x67", tracks + cluster);
  char* error = nullptr;
  char* out = se_mkv_extract(path.c_str(), 1, &error);
  ASSERT_NE(out, nullptr) << (error ? error : "");
  json e = take(out);
  ASSERT_EQ(e["cues"].size(), 1u);
  EXPECT_NE(e["cues"][0]["text"].get<std::string>().find("bad"), std::string::npos);
}

TEST(MkvCapi, ListsAllSubtitleTracks) {
  char* error = nullptr;
  char* out = se_mkv_subtitle_tracks(fixture("subs_mixed.mkv").c_str(), &error);
  ASSERT_NE(out, nullptr);
  json t = take(out);
  ASSERT_EQ(t.size(), 2u);
  EXPECT_FALSE(t[0]["text"].get<bool>());
  EXPECT_TRUE(t[1]["text"].get<bool>());
  EXPECT_EQ(t[1]["codec"], "S_TEXT/UTF8");
}
