#include "subtitle_engine/audio_reader.hpp"

#include <algorithm>
#include <mutex>

#include "avio_bridge.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/log.h>
#include <libswresample/swresample.h>
}

namespace subtitle_engine {

struct AudioReader::Impl {
  explicit Impl(MediaSource& source, const AudioReaderOptions& options)
      : bridge(source, options.waitPoll, options.cancelled) {}
  ~Impl() {
    av_frame_free(&frame);
    av_packet_free(&packet);
    dropResampler();
    avcodec_free_context(&decoder);
    avformat_close_input(&format);
  }

  AvioBridge bridge;
  AVFormatContext* format = nullptr;
  AVCodecContext* decoder = nullptr;
  SwrContext* resampler = nullptr;
  AVChannelLayout resamplerLayout{};  // the input the resampler was set up for: a frame that differs
  int resamplerFormat = -1;           // (a stereo track turning mono, another rate) needs a new one
  int resamplerRate = 0;
  AVPacket* packet = nullptr;
  AVFrame* frame = nullptr;
  std::vector<AudioTrack> tracks;
  size_t chosen = 0;
  int64_t trimMs = -1;      // after a seek: the samples before this film time are dropped
  int64_t nextMs = 0;       // the film time after the last sample given
  bool flushed = false;     // the decoder drained at the end of the media
  std::string lastError;
  std::string endDetail;
  int64_t lastSeekMs = -1;  // the last seek's target, and FFmpeg's answer to it
  int lastSeekResult = 0;
  std::vector<float> converted;

  AVStream* stream() const { return format->streams[tracks[chosen].index]; }
  // Why a read stopped, from what the bridge saw; "" when it was the end of the media.
  std::string failure() const {
    if (bridge.cancelled()) return "cancelled";
    if (bridge.readFailed()) return "io_error";
    return "";
  }
  void convert(const AVFrame* in, std::vector<float>* samples, int64_t* firstSampleMs);
  bool setUpResampler(const AVFrame* in);
  bool fitsResampler(const AVFrame* in) const {
    return resampler && in->format == resamplerFormat && in->sample_rate == resamplerRate &&
           av_channel_layout_compare(&in->ch_layout, &resamplerLayout) == 0;
  }
  void dropResampler() {
    swr_free(&resampler);
    av_channel_layout_uninit(&resamplerLayout);
    resamplerFormat = -1;
    resamplerRate = 0;
  }
};

namespace {

std::string chooseFailure(const AvioBridge& bridge) {
  if (bridge.cancelled()) return "cancelled";
  if (bridge.endedBeforeSize()) return "needs_end_of_file";
  return "io_error";
}

std::vector<AudioTrack> audioTracks(const AVFormatContext* format) {
  std::vector<AudioTrack> out;
  for (unsigned i = 0; i < format->nb_streams; ++i) {
    const AVStream* st = format->streams[i];
    if (st->codecpar->codec_type != AVMEDIA_TYPE_AUDIO) continue;
    const AVDictionaryEntry* lang = av_dict_get(st->metadata, "language", nullptr, 0);
    out.push_back({static_cast<int>(i), avcodec_get_name(st->codecpar->codec_id), lang ? lang->value : "und",
                   (st->disposition & AV_DISPOSITION_DEFAULT) != 0});
  }
  return out;
}

size_t pickTrack(const std::vector<AudioTrack>& tracks, const std::string& language) {
  for (size_t i = 0; i < tracks.size(); ++i)
    if (!language.empty() && tracks[i].language == language) return i;
  for (size_t i = 0; i < tracks.size(); ++i)
    if (tracks[i].isDefault) return i;
  return 0;
}

// The stream to seek on: the first video that is not a cover, since a film's
// index (Cues) lists its video only and a seek on the audio made FFmpeg read
// the file from its start; the audio itself when there is no video.
int seekStream(const AVFormatContext* format, int audio) {
  for (unsigned i = 0; i < format->nb_streams; ++i) {
    const AVStream* s = format->streams[i];
    if (s->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && (s->disposition & AV_DISPOSITION_ATTACHED_PIC) == 0)
      return static_cast<int>(i);
  }
  return audio;
}

}  // namespace

bool AudioReader::Impl::setUpResampler(const AVFrame* in) {
  AVChannelLayout mono = AV_CHANNEL_LAYOUT_MONO;
  AVChannelLayout layout;
  if (in->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC)
    av_channel_layout_default(&layout, in->ch_layout.nb_channels);
  else
    av_channel_layout_copy(&layout, &in->ch_layout);
  const int r = swr_alloc_set_opts2(&resampler, &mono, AV_SAMPLE_FMT_FLT, kSampleRate, &layout,
                                    static_cast<AVSampleFormat>(in->format), in->sample_rate, 0, nullptr);
  av_channel_layout_uninit(&layout);
  if (r < 0 || swr_init(resampler) < 0 || av_channel_layout_copy(&resamplerLayout, &in->ch_layout) < 0) {
    dropResampler();
    return false;
  }
  resamplerFormat = in->format;
  resamplerRate = in->sample_rate;
  return true;
}

// Resamples a frame (or, with in == nullptr, what the resampler still holds)
// and appends the samples at or after trimMs.
void AudioReader::Impl::convert(const AVFrame* in, std::vector<float>* samples, int64_t* firstSampleMs) {
  if (!resampler) return;
  const int inCount = in ? in->nb_samples : 0;
  const int maxOut = swr_get_out_samples(resampler, inCount);
  if (maxOut <= 0) return;
  converted.resize(static_cast<size_t>(maxOut));
  auto* out = reinterpret_cast<uint8_t*>(converted.data());
  const int got = swr_convert(resampler, &out, maxOut, in ? const_cast<const uint8_t**>(in->extended_data) : nullptr,
                              inCount);
  if (got <= 0) return;
  int64_t startMs = nextMs;
  if (in && in->best_effort_timestamp != AV_NOPTS_VALUE)
    startMs = av_rescale_q(in->best_effort_timestamp, stream()->time_base, AVRational{1, 1000});
  int skip = 0;
  if (trimMs >= 0) {
    const int64_t endMs = startMs + static_cast<int64_t>(got) * 1000 / kSampleRate;
    if (endMs <= trimMs) {
      nextMs = endMs;
      return;  // before the point asked for
    }
    if (startMs < trimMs) skip = static_cast<int>((trimMs - startMs) * kSampleRate / 1000);
    startMs = std::max(startMs, trimMs);
    trimMs = -1;
  }
  if (*firstSampleMs < 0) *firstSampleMs = startMs;
  samples->insert(samples->end(), converted.begin() + skip, converted.begin() + got);
  nextMs = startMs + static_cast<int64_t>(got - skip) * 1000 / kSampleRate;
}

std::unique_ptr<AudioReader> AudioReader::open(MediaSource& source, AudioReaderOptions options,
                                               std::string* errorCode) {
  // FFmpeg's messages could carry a file's titles or paths: the engine writes nothing to stderr.
  static std::once_flag quiet;
  std::call_once(quiet, [] { av_log_set_level(AV_LOG_QUIET); });
  auto impl = std::make_unique<Impl>(source, options);
  impl->format = avformat_alloc_context();
  impl->packet = av_packet_alloc();
  impl->frame = av_frame_alloc();
  if (!impl->bridge.context() || !impl->format || !impl->packet || !impl->frame) {
    *errorCode = "io_error";
    return nullptr;
  }
  impl->format->pb = impl->bridge.context();
  impl->format->flags |= AVFMT_FLAG_CUSTOM_IO;
  if (avformat_open_input(&impl->format, nullptr, nullptr, nullptr) < 0) {
    *errorCode = chooseFailure(impl->bridge);  // avformat_open_input frees the context on failure
    return nullptr;
  }
  if (avformat_find_stream_info(impl->format, nullptr) < 0 && (impl->bridge.cancelled() || impl->bridge.readFailed() ||
                                                               impl->bridge.endedBeforeSize())) {
    *errorCode = chooseFailure(impl->bridge);
    return nullptr;
  }
  if (impl->bridge.endedBeforeSize()) {  // the index is past what can be read (MP4 with moov at the end)
    *errorCode = "needs_end_of_file";
    return nullptr;
  }
  impl->tracks = audioTracks(impl->format);
  if (impl->tracks.empty()) {
    *errorCode = "no_audio";
    return nullptr;
  }
  impl->chosen = pickTrack(impl->tracks, options.trackLanguage);
  AVStream* st = impl->stream();
  for (unsigned i = 0; i < impl->format->nb_streams; ++i)
    if (impl->format->streams[i] != st) impl->format->streams[i]->discard = AVDISCARD_ALL;
  const AVCodec* codec = avcodec_find_decoder(st->codecpar->codec_id);
  impl->decoder = codec ? avcodec_alloc_context3(codec) : nullptr;
  if (!impl->decoder || avcodec_parameters_to_context(impl->decoder, st->codecpar) < 0 ||
      avcodec_open2(impl->decoder, codec, nullptr) < 0) {
    *errorCode = "unsupported_codec";
    return nullptr;
  }
  impl->bridge.clearFlags();
  return std::unique_ptr<AudioReader>(new AudioReader(std::move(impl)));
}

AudioReader::AudioReader(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
AudioReader::~AudioReader() = default;

const std::vector<AudioTrack>& AudioReader::tracks() const { return impl_->tracks; }
const AudioTrack& AudioReader::chosen() const { return impl_->tracks[impl_->chosen]; }
const std::string& AudioReader::lastErrorCode() const { return impl_->lastError; }
const std::string& AudioReader::endDetail() const { return impl_->endDetail; }

bool AudioReader::seek(int64_t positionMs) {
  Impl& m = *impl_;
  m.bridge.clearFlags();
  const int on = seekStream(m.format, m.tracks[m.chosen].index);
  const int64_t ts = av_rescale_q(positionMs, AVRational{1, 1000}, m.format->streams[on]->time_base);
  m.lastSeekMs = positionMs;
  m.lastSeekResult = av_seek_frame(m.format, on, ts, AVSEEK_FLAG_BACKWARD);
  if (m.lastSeekResult < 0) return false;
  avcodec_flush_buffers(m.decoder);
  m.dropResampler();
  m.trimMs = positionMs;
  m.nextMs = positionMs;
  m.flushed = false;
  return true;
}

PullResult AudioReader::pull(std::vector<float>* samples, int64_t* firstSampleMs) {
  Impl& m = *impl_;
  *firstSampleMs = -1;
  const size_t before = samples->size();
  for (;;) {
    if (m.flushed) return PullResult::kEndOfMedia;
    const int r = av_read_frame(m.format, m.packet);
    if (r < 0) {
      m.lastError = m.failure();
      if (m.lastError == "cancelled") return PullResult::kCancelled;
      if (!m.lastError.empty()) return PullResult::kError;
      const int64_t durationMs = m.format->duration > 0 ? m.format->duration / 1000 : -1;
      m.endDetail = "read " + std::to_string(r) + " at byte " + std::to_string(avio_tell(m.format->pb)) + ", size " +
                    std::to_string(avio_size(m.format->pb)) + ", film " + std::to_string(m.nextMs) + " of " +
                    std::to_string(durationMs) + " ms, last seek " + std::to_string(m.lastSeekMs) +
                    " ms: " + std::to_string(m.lastSeekResult);
      avcodec_send_packet(m.decoder, nullptr);  // the end: drain the decoder and the resampler
    } else if (m.packet->stream_index != m.tracks[m.chosen].index) {
      av_packet_unref(m.packet);
      continue;
    } else {
      const int sent = avcodec_send_packet(m.decoder, m.packet);
      av_packet_unref(m.packet);
      if (sent < 0 && sent != AVERROR(EAGAIN) && sent != AVERROR_INVALIDDATA) {
        m.lastError = "decode_error";
        return PullResult::kError;
      }
    }
    while (avcodec_receive_frame(m.decoder, m.frame) >= 0) {
      if (!m.fitsResampler(m.frame)) {  // the first frame, or the input changed: give out what the old one holds
        m.convert(nullptr, samples, firstSampleMs);
        m.dropResampler();
      }
      if (!m.resampler && !m.setUpResampler(m.frame)) {
        m.lastError = "decode_error";
        return PullResult::kError;
      }
      m.convert(m.frame, samples, firstSampleMs);
      av_frame_unref(m.frame);
    }
    if (r < 0) {
      m.convert(nullptr, samples, firstSampleMs);
      m.flushed = true;
    }
    if (samples->size() > before) return PullResult::kSamples;
  }
}

}  // namespace subtitle_engine
