#include "subtitle_engine/subtitle_store.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <nlohmann/json.hpp>

namespace subtitle_engine {

namespace {
constexpr int kFormat = 2;  // the file's format, raised when it changes; 2 names the model

// The whole text in [path], flushed to the disk before it is closed.
bool writeDurably(const std::string& path, const std::string& text) {
  const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
  if (fd < 0) return false;
  bool ok = true;
  for (size_t done = 0; ok && done < text.size();) {
    const ssize_t n = ::write(fd, text.data() + done, text.size() - done);
    if (n < 0 && errno == EINTR) continue;
    ok = n > 0;
    if (ok) done += static_cast<size_t>(n);
  }
  ok = ok && ::fsync(fd) == 0;
  return (::close(fd) == 0) && ok;
}

// The rename itself to the disk too, where the system allows it.
void syncFolderOf(const std::string& path) {
  const auto slash = path.find_last_of('/');
  const std::string folder = slash == std::string::npos ? "." : (slash == 0 ? "/" : path.substr(0, slash));
  const int fd = ::open(folder.c_str(), O_RDONLY | O_CLOEXEC);
  if (fd < 0) return;
  ::fsync(fd);
  ::close(fd);
}
}

SubtitleStore::SubtitleStore(std::string path) : path_(std::move(path)) {}

std::optional<StoredSubtitles> SubtitleStore::load(const std::string& mediaId, const std::string& targetLanguage,
                                                   std::string* errorCode) const {
  return load(mediaId, targetLanguage, std::nullopt, errorCode);
}

std::optional<StoredSubtitles> SubtitleStore::load(const std::string& mediaId, const std::string& targetLanguage,
                                                   const std::optional<std::string>& model,
                                                   std::string* errorCode) const {
  std::ifstream in(path_, std::ios::binary);
  if (!in) {
    *errorCode = "not_found";
    return std::nullopt;
  }
  const std::string text((std::istreambuf_iterator<char>(in)), {});
  const nlohmann::json j = nlohmann::json::parse(text, nullptr, false);
  StoredSubtitles out;
  try {
    if (j.is_discarded()) throw std::runtime_error("json");
    if (j.at("format").get<int>() != kFormat) {
      *errorCode = "other_format";
      return std::nullopt;
    }
    out.mediaId = j.at("media_id").get<std::string>();
    out.model = j.at("model").get<std::string>();
    out.targetLanguage = j.at("target_language").get<std::string>();
    out.audioLanguage = j.at("audio_language").get<std::string>();
    for (const auto& r : j.at("ranges")) out.ranges.add(r.at(0).get<int64_t>(), r.at(1).get<int64_t>());
    for (const auto& c : j.at("cues"))
      out.cues.push_back({c.at("start_ms").get<int64_t>(), c.at("end_ms").get<int64_t>(), c.at("text").get<std::string>()});
  } catch (const std::exception&) {
    *errorCode = "corrupt_data";
    return std::nullopt;
  }
  if (out.mediaId != mediaId || out.targetLanguage != targetLanguage) {
    *errorCode = "other_media";
    return std::nullopt;
  }
  if (model && out.model != *model) {
    *errorCode = "other_model";
    return std::nullopt;
  }
  return out;
}

bool SubtitleStore::save(const StoredSubtitles& s, std::string* errorCode) const {
  nlohmann::json j = {{"format", kFormat},
                      {"media_id", s.mediaId},
                      {"target_language", s.targetLanguage},
                      {"audio_language", s.audioLanguage},
                      {"model", s.model},
                      {"ranges", nlohmann::json::array()},
                      {"cues", nlohmann::json::array()}};
  for (const auto& r : s.ranges.spans()) j["ranges"].push_back({r.first, r.second});
  for (const auto& c : s.cues) j["cues"].push_back({{"start_ms", c.startMs}, {"end_ms", c.endMs}, {"text", c.text}});
  const std::string part = path_ + ".part";
  bool ok = writeDurably(part, j.dump());
  ok = ok && std::rename(part.c_str(), path_.c_str()) == 0;
  if (ok) syncFolderOf(path_);
  if (!ok) {
    std::remove(part.c_str());
    *errorCode = "store_failed";
  }
  return ok;
}

}  // namespace subtitle_engine
