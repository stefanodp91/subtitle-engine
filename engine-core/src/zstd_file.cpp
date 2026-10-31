#include "subtitle_engine/zstd_file.hpp"

#include <zstd.h>

#include <cstdio>
#include <memory>
#include <vector>

namespace subtitle_engine {

namespace {

struct FileCloser {
  void operator()(FILE* f) const { fclose(f); }
};

struct StreamFree {
  void operator()(ZSTD_DStream* s) const { ZSTD_freeDStream(s); }
};

}  // namespace

std::string decompressZstdFile(const std::string& inPath, const std::string& outPath, uint64_t maxBytes) {
  std::unique_ptr<FILE, FileCloser> in(fopen(inPath.c_str(), "rb"));
  if (!in) return "io_error";
  const std::string partPath = outPath + ".part";
  std::unique_ptr<FILE, FileCloser> out(fopen(partPath.c_str(), "wb"));
  if (!out) return "io_error";
  auto fail = [&](const char* code) {
    out.reset();
    std::remove(partPath.c_str());
    return std::string(code);
  };

  std::unique_ptr<ZSTD_DStream, StreamFree> stream(ZSTD_createDStream());
  if (!stream || ZSTD_isError(ZSTD_initDStream(stream.get()))) return fail("io_error");
  std::vector<char> inBuffer(ZSTD_DStreamInSize()), outBuffer(ZSTD_DStreamOutSize());
  uint64_t written = 0;
  size_t lastResult = 0;
  bool sawInput = false;
  for (size_t read; (read = fread(inBuffer.data(), 1, inBuffer.size(), in.get())) > 0;) {
    sawInput = true;
    ZSTD_inBuffer input{inBuffer.data(), read, 0};
    while (input.pos < input.size) {
      ZSTD_outBuffer output{outBuffer.data(), outBuffer.size(), 0};
      lastResult = ZSTD_decompressStream(stream.get(), &output, &input);
      if (ZSTD_isError(lastResult)) return fail("corrupt_data");
      written += output.pos;
      if (written > maxBytes) return fail("too_large");
      if (fwrite(outBuffer.data(), 1, output.pos, out.get()) != output.pos) return fail("io_error");
    }
  }
  if (ferror(in.get())) return fail("io_error");
  // lastResult != 0 means the last frame was cut short.
  if (!sawInput || lastResult != 0) return fail("corrupt_data");
  if (fflush(out.get()) != 0) return fail("io_error");
  out.reset();
  if (std::rename(partPath.c_str(), outPath.c_str()) != 0) {
    std::remove(partPath.c_str());
    return "io_error";
  }
  return "";
}

}  // namespace subtitle_engine
