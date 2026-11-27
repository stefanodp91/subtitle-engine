#pragma once
// The subtitles generated so far for one version of a film, kept in a file the
// caller names (with the version's fingerprint, DEC-858): the stretches already
// heard and their cues, so a film opened again resumes where it was left.

#include <optional>
#include <string>
#include <vector>

#include "subtitle_engine/cue.hpp"
#include "subtitle_engine/ready_ranges.hpp"

namespace subtitle_engine {

struct StoredSubtitles {
  std::string mediaId, targetLanguage, audioLanguage;
  std::string model;  // what made the cues, as the caller names it (a speech model and its version)
  ReadyRanges ranges;
  std::vector<Cue> cues;  // in time order
};

class SubtitleStore {
 public:
  explicit SubtitleStore(std::string path);
  /// Whatever model made the cues. errorCode: not_found, corrupt_data, other_format (a file of an earlier
  /// format, which named no model), other_media (another film's version or another target language).
  std::optional<StoredSubtitles> load(const std::string& mediaId, const std::string& targetLanguage,
                                      std::string* errorCode) const;
  /// Only cues made by [model]; another model's file gives other_model, and is heard again.
  std::optional<StoredSubtitles> load(const std::string& mediaId, const std::string& targetLanguage,
                                      const std::optional<std::string>& model, std::string* errorCode) const;
  /// Written to path.part, flushed to the disk, then renamed over path: the old file stays whole until
  /// then, and a power cut after the save does not leave it empty. errorCode: store_failed.
  bool save(const StoredSubtitles& subtitles, std::string* errorCode) const;

 private:
  std::string path_;
};

}  // namespace subtitle_engine
