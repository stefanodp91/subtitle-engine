#pragma once
// Minimal EBML reader over a FILE*, seeking past what it does not need.

#include <cstdint>
#include <cstdio>
#include <string>

namespace subtitle_engine {

class EbmlReader {
 public:
  explicit EbmlReader(FILE* file);
  int64_t position() const { return pos_; }
  int64_t fileSize() const { return size_; }
  bool seek(int64_t to);
  bool readId(uint32_t* id);                      // marker bit kept, 1–4 bytes
  bool readSize(uint64_t* size, bool* unknown);   // marker bit removed, 1–8 bytes
  bool readBytes(uint64_t count, std::string* out);
  bool readUInt(uint64_t size, uint64_t* value);

 private:
  bool readByte(uint8_t* b);
  FILE* file_;
  int64_t pos_ = 0;
  int64_t size_ = 0;
};

}  // namespace subtitle_engine
