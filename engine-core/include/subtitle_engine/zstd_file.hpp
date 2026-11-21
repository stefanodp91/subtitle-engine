#pragma once
// Streaming decompression of a .zst file to disk, with a size cap.

#include <cstdint>
#include <string>

namespace subtitle_engine {

/// Decompresses inPath into outPath through outPath + ".part", renamed only
/// on success. Returns "" on success, else io_error, corrupt_data or too_large
/// (more than maxBytes); on failure no output file is left behind.
std::string decompressZstdFile(const std::string& inPath, const std::string& outPath, uint64_t maxBytes);

}  // namespace subtitle_engine
