#include "avio_bridge.hpp"

#include <cerrno>
#include <thread>

extern "C" {
#include <libavformat/avio.h>
#include <libavutil/error.h>
#include <libavutil/mem.h>
}

namespace subtitle_engine {

namespace {
constexpr int kBufferSize = 32 * 1024;
}

AvioBridge::AvioBridge(MediaSource& source, std::chrono::milliseconds waitPoll, std::function<bool()> cancelled)
    : source_(source), waitPoll_(waitPoll), isCancelled_(std::move(cancelled)) {
  auto* buffer = static_cast<unsigned char*>(av_malloc(kBufferSize));
  if (buffer) {
    context_ = avio_alloc_context(buffer, kBufferSize, 0, this, &AvioBridge::readPacket, nullptr, &AvioBridge::seek);
    if (!context_) av_free(buffer);
  }
}

AvioBridge::~AvioBridge() {
  if (context_) {
    av_freep(&context_->buffer);
    avio_context_free(&context_);
  }
}

void AvioBridge::clearFlags() {
  endedBeforeSize_ = cancelled_ = readFailed_ = false;
  if (context_) {
    context_->eof_reached = 0;
    context_->error = 0;
  }
}

int AvioBridge::readPacket(void* opaque, uint8_t* buffer, int size) {
  auto* self = static_cast<AvioBridge*>(opaque);
  for (;;) {
    const ReadResult r = self->source_.read(static_cast<uint64_t>(self->position_), buffer, static_cast<size_t>(size));
    switch (r.status) {
      case ReadStatus::kData:
        if (r.bytes == 0) break;  // nothing read yet: as a wait
        self->position_ += static_cast<int64_t>(r.bytes);
        return static_cast<int>(r.bytes);
      case ReadStatus::kEnd: {
        const int64_t whole = self->source_.size();
        if (whole >= 0 && self->position_ < whole) self->endedBeforeSize_ = true;
        return AVERROR_EOF;
      }
      case ReadStatus::kError:
        self->readFailed_ = true;
        return AVERROR(EIO);
      case ReadStatus::kWait:
        break;
    }
    if (self->isCancelled_ && self->isCancelled_()) {
      self->cancelled_ = true;
      return AVERROR_EXIT;
    }
    std::this_thread::sleep_for(self->waitPoll_);
    if (self->isCancelled_ && self->isCancelled_()) {
      self->cancelled_ = true;
      return AVERROR_EXIT;
    }
  }
}

int64_t AvioBridge::seek(void* opaque, int64_t offset, int whence) {
  auto* self = static_cast<AvioBridge*>(opaque);
  const int64_t whole = self->source_.size();
  if (whence & AVSEEK_SIZE) return whole >= 0 ? whole : AVERROR(ENOSYS);
  switch (whence & ~AVSEEK_FORCE) {
    case SEEK_SET: self->position_ = offset; break;
    case SEEK_CUR: self->position_ += offset; break;
    case SEEK_END:
      if (whole < 0) return AVERROR(ENOSYS);
      self->position_ = whole + offset;
      break;
    default: return AVERROR(EINVAL);
  }
  return self->position_;
}

}  // namespace subtitle_engine
