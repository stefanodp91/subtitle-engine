#pragma once
// The film as its caller can read it: a file, a stream server, a download still
// running. The engine never opens files or network connections by itself.

#include <cstddef>
#include <cstdint>

namespace subtitle_engine {

enum class ReadStatus {
  kData,   // bytes were read
  kEnd,    // nothing at or after this offset: the file ends here
  kWait,   // the bytes are not there yet; ask again later
  kError,  // the read failed
};

struct ReadResult {
  ReadStatus status;
  size_t bytes;  // with kData, how many bytes were written to the buffer
};

class MediaSource {
 public:
  virtual ~MediaSource() = default;
  /// Up to length bytes at offset. Should not block for long: kWait instead.
  virtual ReadResult read(uint64_t offset, uint8_t* buffer, size_t length) = 0;
  /// The whole size in bytes, or -1 when unknown.
  virtual int64_t size() const = 0;
};

}  // namespace subtitle_engine
