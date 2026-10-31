#include <gtest/gtest.h>
#include <zstd.h>

#include <fstream>
#include <iterator>
#include <nlohmann/json.hpp>
#include <string>

#include "subtitle_engine_c.h"

using nlohmann::json;

namespace {

std::string tmp(const std::string& name) { return std::string(SE_TMP) + "/" + name; }

std::string readAll(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), {});
}

bool exists(const std::string& path) { return std::ifstream(path).good(); }

std::string sampleText() {
  std::string text;
  while (text.size() < 1024 * 1024) text += "A line of subtitle text, repeated to fill a megabyte.\n";
  return text;
}

std::string compressToFile(const std::string& text, const std::string& name) {
  std::string packed(ZSTD_compressBound(text.size()), '\0');
  size_t size = ZSTD_compress(&packed[0], packed.size(), text.data(), text.size(), 3);
  EXPECT_FALSE(ZSTD_isError(size));
  packed.resize(size);
  std::string path = tmp(name);
  std::ofstream(path, std::ios::binary) << packed;
  return path;
}

std::string errorCode(char* error) {
  if (!error) return "";
  std::string code = json::parse(error)["code"];
  se_free(error);
  return code;
}

}  // namespace

TEST(Zstd, DecompressesFile) {
  const std::string text = sampleText();
  std::string in = compressToFile(text, "ok.zst"), out = tmp("ok.out");
  char* error = nullptr;
  ASSERT_EQ(se_zstd_decompress_file(in.c_str(), out.c_str(), text.size(), &error), 1) << (error ? error : "");
  EXPECT_EQ(readAll(out), text);
}

TEST(Zstd, RejectsCorruptData) {
  std::string in = compressToFile(sampleText(), "corrupt.zst"), out = tmp("corrupt.out");
  std::string bytes = readAll(in);
  for (size_t i = bytes.size() / 2; i < bytes.size() / 2 + 32; ++i) bytes[i] = static_cast<char>(bytes[i] ^ 0x5A);
  std::ofstream(in, std::ios::binary) << bytes;
  char* error = nullptr;
  EXPECT_EQ(se_zstd_decompress_file(in.c_str(), out.c_str(), 4 * 1024 * 1024, &error), 0);
  EXPECT_EQ(errorCode(error), "corrupt_data");
  EXPECT_FALSE(exists(out));
}

TEST(Zstd, StopsAtMaxBytes) {
  std::string in = compressToFile(sampleText(), "big.zst"), out = tmp("big.out");
  char* error = nullptr;
  EXPECT_EQ(se_zstd_decompress_file(in.c_str(), out.c_str(), 1000, &error), 0);
  EXPECT_EQ(errorCode(error), "too_large");
  EXPECT_FALSE(exists(out));
}

TEST(Zstd, ReportsMissingInput) {
  char* error = nullptr;
  EXPECT_EQ(se_zstd_decompress_file("/nope/in.zst", tmp("missing.out").c_str(), 1000, &error), 0);
  EXPECT_EQ(errorCode(error), "io_error");
}
