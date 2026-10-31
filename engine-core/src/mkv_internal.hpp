#pragma once
// Internals of the Matroska reader, exposed for the tests.

#include <optional>
#include <string>

namespace subtitle_engine {

struct ContentEncoding {
  enum class Kind { None, Zlib, HeaderStripping };
  Kind kind = Kind::None;
  std::string strippedBytes;  // ContentCompSettings, for HeaderStripping
};

/// Undoes the track's ContentCompression on one block; nullopt if zlib fails.
std::optional<std::string> decodeBlockPayload(const std::string& payload, const ContentEncoding& encoding);

}  // namespace subtitle_engine
