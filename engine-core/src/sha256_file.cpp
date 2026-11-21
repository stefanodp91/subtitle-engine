#include "subtitle_engine/sha256_file.hpp"

extern "C" {
#include <libavutil/mem.h>
#include <libavutil/sha.h>
}

#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

namespace subtitle_engine {

namespace {

constexpr size_t kBlockBytes = 1 << 20;
constexpr int kSha256Bits = 256;
constexpr size_t kDigestBytes = kSha256Bits / 8;

struct FileCloser {
  void operator()(FILE* f) const { fclose(f); }
};

struct ShaFree {
  void operator()(AVSHA* sha) const { av_free(sha); }
};

}  // namespace

std::string sha256File(const std::string& path, std::string* errorCode) {
  auto fail = [&]() {
    if (errorCode) *errorCode = "io_error";
    return std::string();
  };
  std::unique_ptr<FILE, FileCloser> in(fopen(path.c_str(), "rb"));
  if (!in) return fail();
  std::unique_ptr<AVSHA, ShaFree> sha(av_sha_alloc());
  if (!sha || av_sha_init(sha.get(), kSha256Bits) < 0) return fail();

  std::vector<uint8_t> block(kBlockBytes);
  size_t got = 0;
  while ((got = fread(block.data(), 1, block.size(), in.get())) > 0) av_sha_update(sha.get(), block.data(), got);
  if (ferror(in.get())) return fail();

  uint8_t digest[kDigestBytes];
  av_sha_final(sha.get(), digest);
  static const char kHex[] = "0123456789abcdef";
  std::string hex;
  hex.reserve(kDigestBytes * 2);
  for (uint8_t byte : digest) {
    hex.push_back(kHex[byte >> 4]);
    hex.push_back(kHex[byte & 0x0f]);
  }
  return hex;
}

}  // namespace subtitle_engine
