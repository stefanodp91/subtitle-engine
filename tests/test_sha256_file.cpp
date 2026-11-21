#include <gtest/gtest.h>

#include <fstream>
#include <string>

#include "subtitle_engine/sha256_file.hpp"

using subtitle_engine::sha256File;

namespace {

std::string writeTmp(const std::string& name, const std::string& content) {
  std::string path = std::string(SE_TMP) + "/" + name;
  std::ofstream(path, std::ios::binary) << content;
  return path;
}

}  // namespace

TEST(Sha256File, KnownVectors) {
  std::string error;
  // FIPS 180-2 test vectors.
  EXPECT_EQ(sha256File(writeTmp("sha_empty.bin", ""), &error),
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  EXPECT_EQ(sha256File(writeTmp("sha_abc.bin", "abc"), &error),
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  // Longer than the read buffer: 1 MiB + 1 byte of 'a' (value from shasum -a 256).
  EXPECT_EQ(sha256File(writeTmp("sha_long.bin", std::string(1024 * 1024 + 1, 'a')), &error),
            "4a3f0c0c213adea174f9a3d4c13177315b588bdb2e9c1012d3d0bf0453ca0f6a");
  EXPECT_TRUE(error.empty());
}

TEST(Sha256File, MissingFileIsIoError) {
  std::string error;
  EXPECT_EQ(sha256File(std::string(SE_TMP) + "/sha_missing.bin", &error), "");
  EXPECT_EQ(error, "io_error");
}
