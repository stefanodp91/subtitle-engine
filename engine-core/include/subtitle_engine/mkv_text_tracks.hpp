#pragma once
// Text subtitle tracks inside Matroska files: listing and extraction.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace subtitle_engine {

struct TextTrack {
  uint64_t number = 0;
  std::string codec;          // S_TEXT/UTF8, S_TEXT/ASS, S_TEXT/SSA, S_TEXT/WEBVTT
  std::string language;       // ISO 639-2, "eng" when the file says nothing
  std::string languageBcp47;  // empty when absent
  std::string name;
  bool isDefault = true;
  bool isForced = false;
  bool hearingImpaired = false;
  bool isText = true;  // false for image subtitles (VobSub, PGS…), listed only by listSubtitleTracks
};

struct Cue {
  int64_t startMs = 0;
  int64_t endMs = 0;
  std::string text;  // '\n' between rows; ASS override tags {\…} kept
};

struct Extraction {
  std::vector<Cue> cues;
  bool complete = true;  // false when the file ends before the segment does
};

/// Reads only up to the first cluster. errorCode: io_error, not_matroska.
std::vector<TextTrack> listTextTracks(const std::string& path, std::string* errorCode);

/// Every subtitle track, text or image, in file order: the order in which
/// players number their subtitle tracks. errorCode: io_error, not_matroska.
std::vector<TextTrack> listSubtitleTracks(const std::string& path, std::string* errorCode);

/// Reads the whole file, skipping every block of the other tracks.
/// errorCode: io_error, not_matroska, track_not_found, not_text_track.
std::optional<Extraction> extractTextTrack(const std::string& path, uint64_t trackNumber, std::string* errorCode);

}  // namespace subtitle_engine
