#pragma once
// A MediaSource over a file for the tests: only the first limitBytes can be read
// (beyond them it answers pastLimit, like a download still running), a read
// at or after a chosen offset can fail, and a stretch of bytes can read as zeros
// (a file downloaded with holes).

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <string>

#include "subtitle_engine/media_source.hpp"

class FileSource : public subtitle_engine::MediaSource {
 public:
  FileSource(const std::string& path, int64_t limitBytes, subtitle_engine::ReadStatus pastLimit)
      : file_(std::fopen(path.c_str(), "rb")), limit_(limitBytes), pastLimit_(pastLimit) {
    if (file_) {
      std::fseek(file_, 0, SEEK_END);
      size_ = std::ftell(file_);
    }
  }
  explicit FileSource(const std::string& path) : FileSource(path, INT64_MAX, subtitle_engine::ReadStatus::kEnd) {}
  ~FileSource() override {
    if (file_) std::fclose(file_);
  }
  FileSource(const FileSource&) = delete;
  FileSource& operator=(const FileSource&) = delete;

  subtitle_engine::ReadResult read(uint64_t offset, uint8_t* buffer, size_t length) override {
    using subtitle_engine::ReadStatus;
    if (!file_) return {ReadStatus::kError, 0};
    const int64_t at = static_cast<int64_t>(offset);
    if (at + static_cast<int64_t>(length) > failAt_.load()) return {ReadStatus::kError, 0};
    if (at >= size_) return {ReadStatus::kEnd, 0};
    const int64_t limit = std::min(limit_.load(), size_);
    if (at >= limit) return {pastLimit_, 0};
    const size_t want = static_cast<size_t>(std::min<int64_t>(static_cast<int64_t>(length), limit - at));
    std::fseek(file_, static_cast<long>(at), SEEK_SET);
    const size_t got = std::fread(buffer, 1, want, file_);
    for (size_t i = 0; i < got; ++i)
      if (at + static_cast<int64_t>(i) >= zeroFrom_ && at + static_cast<int64_t>(i) < zeroTo_) buffer[i] = 0;
    return {got > 0 ? ReadStatus::kData : ReadStatus::kError, got};
  }
  int64_t size() const override { return size_; }

  void setLimit(int64_t bytes) { limit_ = bytes; }  // any thread
  void failAt(int64_t offset) { failAt_ = offset; }
  void zeroBetween(int64_t from, int64_t to) {  // before the first read
    zeroFrom_ = from;
    zeroTo_ = to;
  }

 private:
  std::FILE* file_;
  int64_t size_ = -1;
  std::atomic<int64_t> limit_;
  std::atomic<int64_t> failAt_{INT64_MAX};
  subtitle_engine::ReadStatus pastLimit_;
  int64_t zeroFrom_ = 0, zeroTo_ = 0;
};
