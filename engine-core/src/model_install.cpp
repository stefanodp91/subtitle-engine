// ModelStore::install: from the caller's downloads to an installed model.
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

#include <cerrno>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

#include "model_store_files.hpp"
#include "subtitle_engine/model_store.hpp"
#include "subtitle_engine/sha256_file.hpp"
#include "subtitle_engine/zstd_file.hpp"

namespace fs = std::filesystem;

namespace subtitle_engine {

namespace {

// An exclusive lock on <dir>/<id>.lock for the life of the object.
class InstallLock {
 public:
  explicit InstallLock(const std::string& path) : fd_(open(path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0644)) {
    if (fd_ < 0) {
      code_ = "io_error";
    } else if (flock(fd_, LOCK_EX | LOCK_NB) != 0) {
      code_ = errno == EWOULDBLOCK ? "busy" : "io_error";
    }
  }
  ~InstallLock() {
    if (fd_ >= 0) close(fd_);  // closing releases the lock
  }
  InstallLock(const InstallLock&) = delete;
  InstallLock& operator=(const InstallLock&) = delete;

  const std::string& code() const { return code_; }

 private:
  int fd_;
  std::string code_;
};

bool sameHash(const std::string& path, const std::string& expected, std::string* errorCode) {
  std::string error;
  const std::string actual = sha256File(path, &error);
  if (!error.empty()) *errorCode = error;
  return error.empty() && actual == expected;
}

}  // namespace

std::string ModelStore::install(const ModelEntry& model) const {
  std::error_code ec;
  fs::create_directories(dir_, ec);
  InstallLock lock(dir_ + "/" + model.id + kLockSuffix);
  if (!lock.code().empty()) return lock.code();
  std::string code;
  try {
    code = installLocked(model);
  } catch (const fs::filesystem_error&) {
    code = "io_error";
  }
  if (!code.empty()) fs::remove_all(modelDir(model), ec);
  return code;
}

std::string ModelStore::installLocked(const ModelEntry& model) const {
  const std::string staging = stagingDir(model);
  // Every download checked and decompressed before the installed model is touched.
  for (const auto& f : model.files) {
    const std::string staged = stagedPath(model, f);
    std::error_code ec;
    if (!fs::is_regular_file(staged, ec)) return "download_missing";
    if (fs::file_size(staged, ec) != f.size || ec) return "download_corrupt";
    std::string error;
    if (!sameHash(staged, f.sha256, &error)) return error.empty() ? "download_corrupt" : error;
    const std::string plain = staging + "/" + f.name;
    if (f.compression == Compression::kZstd) {
      const std::string decompressError = decompressZstdFile(staged, plain, f.installedSize);
      if (!decompressError.empty()) return decompressError;
      fs::remove(staged);
    }
    if (f.compression == Compression::kZstd || f.installedSha256 != f.sha256) {
      if (!sameHash(plain, f.installedSha256, &error)) return error.empty() ? "installed_corrupt" : error;
    }
  }

  // The marker goes first, so a model half replaced never counts as installed.
  const std::string folder = modelDir(model);
  fs::remove(markerPath(model));
  for (const auto& entry : fs::directory_iterator(folder)) {
    if (entry.path() != fs::path(staging)) fs::remove_all(entry.path());
  }
  for (const auto& f : model.files) fs::rename(staging + "/" + f.name, filePath(model, f));
  const std::string part = markerPath(model) + kPartSuffix;
  {
    std::ofstream out(part, std::ios::binary | std::ios::trunc);
    out << nlohmann::json{{"version", model.version}}.dump();
    if (!out.flush()) return "io_error";
  }
  fs::rename(part, markerPath(model));
  fs::remove_all(staging);
  return "";
}

}  // namespace subtitle_engine
