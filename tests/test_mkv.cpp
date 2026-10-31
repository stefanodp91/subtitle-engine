#include <gtest/gtest.h>
#include <zlib.h>

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

#include "mkv_internal.hpp"
#include "subtitle_engine/mkv_text_tracks.hpp"

using namespace subtitle_engine;

namespace {

std::string fixture(const std::string& name) { return std::string(SE_FIXTURES) + "/" + name; }

std::string readAll(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), {});
}

std::string writeTemp(const std::string& name, const std::string& bytes) {
  std::string path = std::string(SE_TMP) + "/" + name;
  std::ofstream(path, std::ios::binary) << bytes;
  return path;
}

Extraction extractSrt(const std::string& path) {
  std::string error;
  auto tracks = listTextTracks(path, &error);
  EXPECT_EQ(tracks.size(), 1u) << error;
  auto result = extractTextTrack(path, tracks.at(0).number, &error);
  EXPECT_TRUE(result) << error;
  return *result;
}

}  // namespace

TEST(MkvReader, ListsTextTrack) {
  std::string error;
  auto tracks = listTextTracks(fixture("subs_srt.mkv"), &error);
  ASSERT_EQ(tracks.size(), 1u) << error;
  EXPECT_EQ(tracks[0].codec, "S_TEXT/UTF8");
  EXPECT_EQ(tracks[0].language, "eng");
  EXPECT_EQ(tracks[0].name, "English");
}

TEST(MkvReader, ExtractsSrtCues) {
  Extraction e = extractSrt(fixture("subs_srt.mkv"));
  ASSERT_EQ(e.cues.size(), 5u);
  const int64_t starts[] = {2000, 5000, 8000, 11000, 14000};
  for (int i = 0; i < 5; ++i) {
    EXPECT_EQ(e.cues[i].startMs, starts[i]);
    EXPECT_EQ(e.cues[i].endMs, starts[i] + 2000);
  }
  EXPECT_NE(e.cues[1].text.find("<i>"), std::string::npos) << e.cues[1].text;
  EXPECT_NE(e.cues[2].text.find('\n'), std::string::npos) << e.cues[2].text;
  EXPECT_TRUE(e.complete);
}

TEST(MkvReader, ExtractsAssTextField) {
  std::string error;
  auto tracks = listTextTracks(fixture("subs_ass.mkv"), &error);
  ASSERT_EQ(tracks.size(), 1u) << error;
  EXPECT_EQ(tracks[0].codec, "S_TEXT/ASS");
  auto e = extractTextTrack(fixture("subs_ass.mkv"), tracks[0].number, &error);
  ASSERT_TRUE(e) << error;
  ASSERT_EQ(e->cues.size(), 5u);
  EXPECT_EQ(e->cues[1].text, "{\\i1}Second line, in italics.{\\i0}");
  EXPECT_EQ(e->cues[2].text, "Third line\non two rows.");
}

TEST(MkvReader, ExtractsWebVttCues) {
  std::string error;
  auto tracks = listTextTracks(fixture("subs_vtt.mkv"), &error);
  ASSERT_EQ(tracks.size(), 1u) << error;
  EXPECT_EQ(tracks[0].codec, "S_TEXT/WEBVTT");
  auto e = extractTextTrack(fixture("subs_vtt.mkv"), tracks[0].number, &error);
  ASSERT_TRUE(e) << error;
  ASSERT_EQ(e->cues.size(), 5u);
  EXPECT_EQ(e->cues[3].startMs, 11000);
  EXPECT_EQ(e->cues[3].text, "Fourth line.");
}

TEST(MkvReader, DecodesZlibCompressedBlock) {
  const std::string text = "Compressed subtitle line.";
  uLongf size = compressBound(text.size());
  std::string packed(size, '\0');
  ASSERT_EQ(compress(reinterpret_cast<Bytef*>(&packed[0]), &size, reinterpret_cast<const Bytef*>(text.data()),
                     text.size()),
            Z_OK);
  packed.resize(size);
  auto out = decodeBlockPayload(packed, ContentEncoding{ContentEncoding::Kind::Zlib, ""});
  ASSERT_TRUE(out);
  EXPECT_EQ(*out, text);
}

TEST(MkvReader, RestoresStrippedHeader) {
  auto out = decodeBlockPayload("line.", ContentEncoding{ContentEncoding::Kind::HeaderStripping, "A stripped "});
  ASSERT_TRUE(out);
  EXPECT_EQ(*out, "A stripped line.");
}

TEST(MkvReader, ReadsUnknownSizeCluster) {
  std::string bytes = readAll(fixture("subs_srt.mkv"));
  const std::string clusterId("\x1F\x43\xB6\x75", 4);
  size_t at = bytes.find(clusterId);
  ASSERT_NE(at, std::string::npos);
  size_t sizeAt = at + 4;
  unsigned char first = static_cast<unsigned char>(bytes[sizeAt]);
  int length = 1;
  while (length <= 8 && !(first & (0x80 >> (length - 1)))) ++length;
  ASSERT_LE(length, 8);
  bytes[sizeAt] = static_cast<char>((0xFF >> (length - 1)) | (0x80 >> (length - 1)));
  for (int i = 1; i < length; ++i) bytes[sizeAt + i] = static_cast<char>(0xFF);
  Extraction e = extractSrt(writeTemp("unknown_cluster.mkv", bytes));
  EXPECT_EQ(e.cues.size(), 5u);
}

TEST(MkvReader, TruncatedFileReturnsPartial) {
  std::string bytes = readAll(fixture("subs_srt.mkv"));
  bytes.resize(bytes.size() * 6 / 10);
  Extraction e = extractSrt(writeTemp("truncated.mkv", bytes));
  EXPECT_LT(e.cues.size(), 5u);
  EXPECT_FALSE(e.complete);
}
