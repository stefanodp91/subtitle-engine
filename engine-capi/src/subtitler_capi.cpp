// The C interface of the subtitles from the audio: the listener, the queue, the
// subtitles saved for a film's version.

#include <chrono>
#include <cmath>
#include <nlohmann/json.hpp>
#include <string>

#include "capi_common.hpp"
#include "subtitle_engine/listener.hpp"
#include "subtitle_engine/subtitle_store.hpp"
#include "subtitle_engine/subtitler.hpp"
#include "subtitle_engine_c.h"

using nlohmann::json;
using namespace subtitle_engine;
using namespace subtitle_engine::capi;

struct se_listener {
  std::unique_ptr<Listener> listener;
};

namespace {

// The caller's read function as the engine's MediaSource.
class CMediaSource : public MediaSource {
 public:
  explicit CMediaSource(const se_media_source& source) : source_(source) {}
  ReadResult read(uint64_t offset, uint8_t* buffer, size_t length) override {
    const long long r = source_.read(source_.opaque, offset, buffer, length);
    if (r > 0) return {ReadStatus::kData, static_cast<size_t>(r)};
    if (r == SE_READ_END) return {ReadStatus::kEnd, 0};
    if (r == SE_READ_WAIT) return {ReadStatus::kWait, 0};
    return {ReadStatus::kError, 0};
  }
  int64_t size() const override { return source_.size; }

 private:
  se_media_source source_;
};

using Ms = std::chrono::milliseconds;

// A key of the config, when present, into a value of the engine's rules.
template <typename T>
void take(const json& c, const char* key, T* into) {
  if (c.contains(key)) *into = c.at(key).get<T>();
}
void takeMs(const json& c, const char* key, Ms* into) {
  if (c.contains(key)) *into = Ms(c.at(key).get<int64_t>());
}

SubtitlerConfig configFrom(const json& c) {
  SubtitlerConfig cfg;
  PieceRules& p = cfg.pieces;
  for (auto [key, into] : {std::pair{"piece_ms", &p.piece}, {"cut_min_ms", &p.cutMin}, {"first_piece_ms", &p.firstPiece},
                           {"first_cut_ms", &p.firstCut}, {"first_pause_min_ms", &p.firstPauseMin},
                           {"first_pause_open_ms", &p.firstPauseOpen}, {"pause_min_ms", &p.pauseMin},
                           {"min_tail_ms", &p.minTail}, {"first_probe_every_ms", &p.firstProbeEvery},
                           {"stall_ms", &cfg.stall}, {"wait_poll_ms", &cfg.waitPoll}, {"read_ahead_ms", &cfg.readAhead},
                           {"min_on_screen_ms", &cfg.format.minOnScreen}, {"max_on_screen_ms", &cfg.format.maxOnScreen},
                           {"gap_ms", &cfg.format.gap}, {"min_length_ms", &cfg.format.minLength},
                           {"slack_ms", &cfg.format.slack}, {"duplicate_window_ms", &cfg.cleanup.duplicateWindow},
                           {"repeat_pause_ms", &cfg.cleanup.repeatPause}, {"sentence_pause_ms", &cfg.join.pause},
                           {"second_piece_ms", &cfg.second.secondPiece}})
    takeMs(c, key, into);
  for (auto [key, into] : {std::pair{"line_chars", &cfg.format.lineChars}, {"max_lines", &cfg.format.maxLines},
                           {"cut_margin", &cfg.format.cutMargin}, {"break_slack", &cfg.format.breakSlack},
                           {"loop_min_repeats", &cfg.cleanup.loopMinRepeats}, {"loop_keep", &cfg.cleanup.loopKeep},
                           {"loop_phrase_chars", &cfg.cleanup.loopPhraseChars},
                           {"duplicate_min_words", &cfg.cleanup.duplicateMinWords},
                           {"duplicate_look_back", &cfg.cleanup.duplicateLookBack},
                           {"prompt_min_words", &cfg.cleanup.promptMinWords},
                           {"prompt_name_min_chars", &cfg.cleanup.promptNameMinChars},
                           {"dense_chars_per_s", &cfg.rehear.denseCharsPerSecond},
                           {"dense_min_chars", &cfg.rehear.denseMinChars}})
    take(c, key, into);
  for (auto [key, into] : {std::pair{"prompt_share", &cfg.cleanup.promptShare}, {"double_margin", &cfg.second.margin},
                           {"double_fill_sure", &cfg.second.fillSure}, {"loop_ratio", &cfg.second.loopRatio}})
    take(c, key, into);
  take(c, "temperature_inc", &cfg.temperatureInc);
  take(c, "double_listen", &cfg.second.enabled);
  take(c, "retry_languages", &cfg.retryLanguages);
  take(c, "credit_patterns", &cfg.cleanup.creditPatterns);
  return cfg;
}

AlignmentHeads headsFrom(const std::string& name) {
  if (name == "none") return AlignmentHeads::kNone;
  if (name == "base") return AlignmentHeads::kBase;
  if (name == "small") return AlignmentHeads::kSmall;
  if (name == "large_v3_turbo") return AlignmentHeads::kLargeV3Turbo;
  throw std::invalid_argument("alignment_heads");
}

const char* statusName(StepStatus s) {
  switch (s) {
    case StepStatus::kCues: return "cues";
    case StepStatus::kEndOfMedia: return "end_of_media";
    case StepStatus::kCancelled: return "cancelled";
    case StepStatus::kTranslatorNeeded: return "translator_needed";
    case StepStatus::kError: break;
  }
  return "error";
}

json cuesJson(const std::vector<Cue>& cues) {
  json out = json::array();
  for (const auto& c : cues) out.push_back({{"start_ms", c.startMs}, {"end_ms", c.endMs}, {"text", c.text}});
  return out;
}

}  // namespace

struct se_subtitler {
  std::unique_ptr<CMediaSource> source;
  std::unique_ptr<Subtitler> subtitler;
};

extern "C" {

SE_API se_listener* se_listener_open(const char* specJson, char** errorJson) try {
  ListenerSpec spec;
  try {
    const json j = json::parse(specJson ? specJson : "");
    spec.modelPath = j.at("model").get<std::string>();
    spec.vadPath = j.at("vad").get<std::string>();
    take(j, "threads", &spec.threads);
    take(j, "vad_threads", &spec.vadThreads);
    if (j.contains("alignment_heads")) spec.heads = headsFrom(j.at("alignment_heads").get<std::string>());
    takeMs(j, "end_pad_ms", &spec.timing.endPad);
    takeMs(j, "moved_min_ms", &spec.timing.movedMin);
    takeMs(j, "start_before_end_ms", &spec.timing.startBeforeEnd);
  } catch (const std::exception&) {
    setError(errorJson, "invalid_argument", "spec_json is not a valid listener spec");
    return nullptr;
  }
  std::string code;
  auto listener = Listener::open(spec, &code);
  if (!listener) {
    setError(errorJson, code);
    return nullptr;
  }
  return new se_listener{std::move(listener)};
} catch (...) {
  setError(errorJson, "model_load_failed");
  return nullptr;
}

SE_API void se_listener_close(se_listener* listener) { delete listener; }

SE_API se_subtitler* se_subtitler_open(se_listener* listener, const se_media_source* source, const char* specJson,
                                       se_progress_fn progress, void* progressOpaque, char** errorJson) try {
  if (!listener || !source || !source->read) {
    setError(errorJson, "invalid_argument");
    return nullptr;
  }
  SubtitlerSpec spec;
  try {
    const json j = json::parse(specJson ? specJson : "");
    spec.targetLanguage = j.at("target_language").get<std::string>();
    take(j, "track_language", &spec.trackLanguage);
    take(j, "prompt", &spec.prompt);
    if (j.contains("store")) {
      spec.storePath = j.at("store").at("path").get<std::string>();
      spec.mediaId = j.at("store").at("media_id").get<std::string>();
      take(j.at("store"), "model", &spec.storeModel);
    }
    if (j.contains("config")) spec.config = configFrom(j.at("config"));
  } catch (const std::exception&) {
    setError(errorJson, "invalid_argument", "spec_json is not a valid subtitler spec");
    return nullptr;
  }
  Progress report;
  if (progress) report = [progress, progressOpaque](Stage s, double f) { progress(progressOpaque, static_cast<int>(s), f); };
  auto out = std::make_unique<se_subtitler>();
  out->source = std::make_unique<CMediaSource>(*source);
  std::string code;
  out->subtitler = Subtitler::open(*listener->listener, *out->source, std::move(spec), std::move(report), &code);
  if (!out->subtitler) {
    setError(errorJson, code);
    return nullptr;
  }
  return out.release();
} catch (...) {
  setError(errorJson, "io_error");
  return nullptr;
}

SE_API int se_subtitler_set_translator(se_subtitler* subtitler, se_translator* translator, char** errorJson) {
  if (!subtitler) {
    setError(errorJson, "invalid_argument");
    return 0;
  }
  subtitler->subtitler->setTranslator(translator ? translator->translator.get() : nullptr);
  return 1;
}

SE_API char* se_subtitler_step(se_subtitler* subtitler, char** errorJson) try {
  if (!subtitler) {
    setError(errorJson, "invalid_argument");
    return nullptr;
  }
  const Step s = subtitler->subtitler->step();
  if (s.status == StepStatus::kError) {
    setError(errorJson, s.errorCode);
    return nullptr;
  }
  json reply{{"status", statusName(s.status)},
             {"from_ms", s.fromMs},
             {"ready_until_ms", s.readyUntilMs},
             {"audio_language", s.audioLanguage},
             {"cues", cuesJson(s.cues)}};
  if (!s.endDetail.empty()) reply["end_detail"] = s.endDetail;
  if (s.saveMs >= 0) reply["save_ms"] = std::round(s.saveMs * 10) / 10;
  return duplicate(toJson(reply));
} catch (...) {
  setError(errorJson, "io_error");
  return nullptr;
}

SE_API void se_subtitler_seek(se_subtitler* subtitler, long long positionMs) {
  if (subtitler) subtitler->subtitler->seek(positionMs);
}

SE_API void se_subtitler_cancel(se_subtitler* subtitler) {
  if (subtitler) subtitler->subtitler->cancel();
}

SE_API void se_subtitler_close(se_subtitler* subtitler) { delete subtitler; }

namespace {

// What a load gives the caller: the stretches and their cues, or NULL and the error's code.
char* savedJson(const std::optional<StoredSubtitles>& saved, const std::string& code, char** errorJson) {
  if (!saved) {
    setError(errorJson, code);
    return nullptr;
  }
  json ranges = json::array();
  for (const auto& r : saved->ranges.spans()) ranges.push_back({r.first, r.second});
  return duplicate(toJson(json{{"audio_language", saved->audioLanguage}, {"ranges", ranges}, {"cues", cuesJson(saved->cues)}}));
}

}  // namespace

SE_API char* se_subtitles_load(const char* path, const char* mediaId, const char* targetLanguage, char** errorJson) try {
  if (!path || !mediaId || !targetLanguage) {
    setError(errorJson, "invalid_argument");
    return nullptr;
  }
  std::string code;
  auto saved = SubtitleStore(path).load(mediaId, targetLanguage, &code);
  return savedJson(saved, code, errorJson);
} catch (...) {
  setError(errorJson, "corrupt_data");
  return nullptr;
}

SE_API char* se_subtitles_load_store(const char* storeJson, const char* targetLanguage, char** errorJson) try {
  std::string path, mediaId, model;
  try {
    const json store = json::parse(storeJson ? storeJson : "");
    path = store.at("path").get<std::string>();
    mediaId = store.at("media_id").get<std::string>();
    take(store, "model", &model);
  } catch (const std::exception&) {
    setError(errorJson, "invalid_argument", "store_json is not a valid store");
    return nullptr;
  }
  if (!targetLanguage) {
    setError(errorJson, "invalid_argument");
    return nullptr;
  }
  std::string code;
  auto saved = SubtitleStore(path).load(mediaId, targetLanguage, model, &code);
  return savedJson(saved, code, errorJson);
} catch (...) {
  setError(errorJson, "corrupt_data");
  return nullptr;
}

}  // extern "C"
