#include "subtitle_engine/listener.hpp"

#include <algorithm>
#include <cmath>

#include "cue_timing.hpp"
#include "subtitle_engine/audio_reader.hpp"
#include "whisper.h"

namespace subtitle_engine {

namespace {

constexpr int kMaxAudioCtx = 1500;     // whisper's encoder on 30 s
constexpr int kAudioCtxPerSecond = 50;  // its frames per second of audio
constexpr int kAudioCtxPad = 8;         // the margin the feasibility probe used
constexpr int kCentisecondMs = 10;      // whisper's times are in centiseconds

void quiet(enum ggml_log_level, const char*, void*) {}  // the engine writes nothing to stderr

whisper_alignment_heads_preset preset(AlignmentHeads heads) {
  switch (heads) {
    case AlignmentHeads::kBase: return WHISPER_AHEADS_BASE;
    case AlignmentHeads::kSmall: return WHISPER_AHEADS_SMALL;
    case AlignmentHeads::kLargeV3Turbo: return WHISPER_AHEADS_LARGE_V3_TURBO;
    case AlignmentHeads::kNone: break;
  }
  return WHISPER_AHEADS_NONE;
}

// Whether a cue is speech: letters left once "(music)", "[music]" and "♪" are taken out.
bool spoken(const std::string& t) {
  int depth = 0;
  for (size_t i = 0; i < t.size(); ++i) {
    if (t.compare(i, 3, "\xE2\x99\xAA") == 0) {
      i += 2;
      continue;
    }
    const auto c = static_cast<unsigned char>(t[i]);
    if (c == '(' || c == '[') ++depth;
    else if ((c == ')' || c == ']') && depth > 0) --depth;
    else if (depth == 0 && ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c >= 0x80)) return true;
  }
  return false;
}

bool abortWhenCancelled(void* user) {
  const auto* cancelled = static_cast<const std::function<bool()>*>(user);
  return *cancelled && (*cancelled)();
}

}  // namespace

struct Listener::Impl {
  ~Impl() {
    if (vad) whisper_vad_free(vad);
    if (ctx) whisper_free(ctx);
  }
  ListenerSpec spec;
  whisper_context* ctx = nullptr;
  whisper_vad_context* vad = nullptr;
};

std::unique_ptr<Listener> Listener::open(const ListenerSpec& spec, std::string* errorCode) {
  if (spec.modelPath.empty() || spec.vadPath.empty() || spec.threads < 1 || spec.vadThreads < 1) {
    *errorCode = "invalid_argument";
    return nullptr;
  }
  whisper_log_set(quiet, nullptr);
  auto impl = std::make_unique<Impl>();
  impl->spec = spec;
  whisper_context_params cparams = whisper_context_default_params();
  cparams.use_gpu = false;
  cparams.flash_attn = false;  // it turns the alignment heads off
  cparams.dtw_token_timestamps = spec.heads != AlignmentHeads::kNone;
  cparams.dtw_aheads_preset = preset(spec.heads);
  impl->ctx = whisper_init_from_file_with_params(spec.modelPath.c_str(), cparams);
  whisper_vad_context_params vparams = whisper_vad_default_context_params();
  vparams.n_threads = spec.vadThreads;
  vparams.use_gpu = false;
  if (impl->ctx) impl->vad = whisper_vad_init_from_file_with_params(spec.vadPath.c_str(), vparams);
  if (!impl->ctx || !impl->vad) {
    *errorCode = "model_load_failed";
    return nullptr;
  }
  return std::unique_ptr<Listener>(new Listener(std::move(impl)));
}

Listener::Listener(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
Listener::~Listener() = default;

std::vector<SpeechSpan> Listener::speech(const float* samples, size_t count, int64_t) {
  std::vector<SpeechSpan> out;
  whisper_vad_segments* seg =
      whisper_vad_segments_from_samples(impl_->vad, whisper_vad_default_params(), samples, static_cast<int>(count));
  if (!seg) return out;
  for (int i = 0; i < whisper_vad_segments_n_segments(seg); ++i)
    out.push_back({std::llround(whisper_vad_segments_get_segment_t0(seg, i) * kCentisecondMs),
                   std::llround(whisper_vad_segments_get_segment_t1(seg, i) * kCentisecondMs)});
  whisper_vad_free_segments(seg);
  return out;
}

Heard Listener::hear(const float* samples, size_t count, int64_t startMs, const HearOptions& options) {
  Heard heard;
  whisper_full_params wp = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
  wp.n_threads = impl_->spec.threads;
  wp.print_progress = wp.print_realtime = wp.print_timestamps = wp.print_special = false;
  wp.language = options.language.empty() ? "auto" : options.language.c_str();
  wp.temperature_inc = options.temperatureInc;
  if (!options.prompt.empty()) {
    wp.initial_prompt = options.prompt.c_str();
    wp.carry_initial_prompt = true;
  }
  // The encoder on the piece's length, not always on 30 s.
  const double seconds = static_cast<double>(count) / kSampleRate;
  wp.audio_ctx = std::min(kMaxAudioCtx, static_cast<int>(std::ceil(seconds * kAudioCtxPerSecond)) + kAudioCtxPad);
  wp.abort_callback = abortWhenCancelled;
  wp.abort_callback_user_data = const_cast<std::function<bool()>*>(&options.cancelled);
  if (options.cancelled && options.cancelled()) {
    heard.cancelled = true;
    return heard;
  }
  whisper_context* ctx = impl_->ctx;
  if (whisper_full(ctx, wp, samples, static_cast<int>(count)) != 0 || (options.cancelled && options.cancelled())) {
    heard.cancelled = options.cancelled && options.cancelled();
    return heard;
  }
  heard.detectedLanguage = options.language.empty() ? whisper_lang_str(whisper_full_lang_id(ctx)) : options.language;
  const whisper_token eot = whisper_token_eot(ctx);
  for (int s = 0; s < whisper_full_n_segments(ctx); ++s) {
    std::string text = whisper_full_get_segment_text(ctx, s);
    const size_t b = text.find_first_not_of(' ');
    if (b == std::string::npos) continue;
    text = text.substr(b);
    if (spoken(text)) heard.spoken = true;
    HeardCue hc;
    hc.cue = {startMs + whisper_full_get_segment_t0(ctx, s) * kCentisecondMs,
              startMs + whisper_full_get_segment_t1(ctx, s) * kCentisecondMs, text};
    double sum = 0;
    int64_t firstWord = -1, lastWord = -1;
    for (int k = 0; k < whisper_full_n_tokens(ctx, s); ++k) {
      if (whisper_full_get_token_id(ctx, s, k) >= eot) continue;
      const whisper_token_data d = whisper_full_get_token_data(ctx, s, k);
      sum += d.p;
      ++hc.tokens;
      if (hc.tokens == 1) firstWord = d.t_dtw < 0 ? -1 : startMs + d.t_dtw * kCentisecondMs;
      lastWord = d.t_dtw < 0 ? -1 : startMs + d.t_dtw * kCentisecondMs;
    }
    hc.sure = hc.tokens ? sum / hc.tokens : 0.0;
    hc.cue = alignCue(hc.cue, firstWord, lastWord, impl_->spec.timing);
    heard.cues.push_back(std::move(hc));
  }
  return heard;
}

}  // namespace subtitle_engine
