#pragma once
// SHA-256 of a file, read in blocks.

#include <string>

namespace subtitle_engine {

/// The SHA-256 of the file at path, as 64 lowercase hex digits, read in blocks
/// (never the whole file in memory). Returns "" and sets *errorCode to io_error
/// when the file cannot be read.
std::string sha256File(const std::string& path, std::string* errorCode);

}  // namespace subtitle_engine
