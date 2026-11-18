#pragma once
// FFmpeg's I/O over a MediaSource: FFmpeg asks for bytes, the caller's read
// function gives them. Bytes not there yet are asked again every waitPoll inside
// the read itself (FFmpeg does not take a "try again" in the middle of a packet),
// until they arrive or the caller cancels.

#include <chrono>
#include <cstdint>
#include <functional>

#include "subtitle_engine/media_source.hpp"

struct AVIOContext;

namespace subtitle_engine {

class AvioBridge {
 public:
  AvioBridge(MediaSource& source, std::chrono::milliseconds waitPoll, std::function<bool()> cancelled);
  ~AvioBridge();
  AvioBridge(const AvioBridge&) = delete;
  AvioBridge& operator=(const AvioBridge&) = delete;

  AVIOContext* context() const { return context_; }
  bool endedBeforeSize() const { return endedBeforeSize_; }  // a kEnd before size(): the file stops short
  bool cancelled() const { return cancelled_; }              // a wait ended by the caller
  bool readFailed() const { return readFailed_; }            // a kError
  void clearFlags();

 private:
  static int readPacket(void* opaque, uint8_t* buffer, int size);
  static int64_t seek(void* opaque, int64_t offset, int whence);

  MediaSource& source_;
  std::chrono::milliseconds waitPoll_;
  std::function<bool()> isCancelled_;
  AVIOContext* context_ = nullptr;
  int64_t position_ = 0;
  bool endedBeforeSize_ = false;
  bool cancelled_ = false;
  bool readFailed_ = false;
};

}  // namespace subtitle_engine
