// The queue's steps: listen, clean, join, translate, shape; jumps and the
// translator that may come later.

#include "subtitle_engine/subtitler.hpp"

#include <algorithm>

#include "sentence_joiner.hpp"
#include "subtitler_impl.hpp"
#include "translation_stage.hpp"

namespace subtitle_engine {

namespace {
constexpr double kStageShare = 1.0 / 3;  // each stage's share of a step's progress
}

std::unique_ptr<Subtitler> Subtitler::open(SpeechRecognizer& recognizer, MediaSource& source, SubtitlerSpec spec,
                                           Progress progress, std::string* errorCode) {
  auto impl = std::make_unique<Impl>(recognizer, std::move(spec), std::move(progress));
  const auto& cfg = impl->spec.config;
  impl->feed = AudioFeed::open(source, impl->spec.trackLanguage, cfg.waitPoll, cfg.readAhead, errorCode);
  if (!impl->feed) return nullptr;
  // The track says its language (or the caller does): the whole film in it. Else whisper guesses.
  impl->language = whisperCode(impl->spec.trackLanguage.empty() ? impl->feed->track().language : impl->spec.trackLanguage);
  if (!impl->spec.storePath.empty()) {
    impl->store.emplace(impl->spec.storePath);
    std::string ignored;  // not found, broken or another version: started over, rewritten at the first step
    if (auto saved = impl->store->load(impl->spec.mediaId, impl->spec.targetLanguage, &ignored)) {
      impl->ready = saved->ranges;
      impl->storedCues = std::move(saved->cues);
      if (impl->language.empty()) impl->language = saved->audioLanguage;
    }
  }
  return std::unique_ptr<Subtitler>(new Subtitler(std::move(impl)));
}

Subtitler::Subtitler(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
Subtitler::~Subtitler() = default;

void Subtitler::setTranslator(TextTranslator* translator) { impl_->translator = translator; }

void Subtitler::seek(int64_t positionMs) {
  Impl& m = *impl_;
  {
    std::lock_guard<std::mutex> lock(m.readyMutex);
    if (m.ready.contains(positionMs)) return;  // heard already: the queue keeps going where it is
  }
  m.pendingSeek = positionMs;
  ++m.generation;
  m.feed->seek(positionMs);
}

void Subtitler::cancel() {
  impl_->cancelled = true;
  ++impl_->generation;
}

void Subtitler::Impl::report(Stage stage, double fraction) {
  stage = std::max(stage, lastStage);  // never back to an earlier stage within a step
  const double at = std::max(lastFraction, (static_cast<int>(stage) + std::clamp(fraction, 0.0, 1.0)) * kStageShare);
  lastStage = stage;
  lastFraction = at;
  if (progress) progress(stage, std::min(at, 1.0));
}

// After a jump outside what is ready: everything starts over at the new place.
void Subtitler::Impl::restartAt(int64_t positionMs) {
  first = {positionMs, 0, -1};
  second = {positionMs, 1, -1};
  carryFirst.clear();
  carrySecond.clear();
  cleaner.reset();
  previous.reset();
  waiting.reset();
  nextFromMs = positionMs;
  ended = false;
}

// Where the queue would start is ready already (saved before, or a jump back
// into what was heard): it goes on where that stretch ends, short again (DEC-845).
void Subtitler::Impl::skipReady() {
  int64_t until;
  {
    std::lock_guard<std::mutex> lock(readyMutex);
    if (!ready.contains(nextFromMs)) return;
    until = ready.endOf(nextFromMs);
  }
  restartAt(until);
  feed->seek(until);
  for (const Cue& c : storedCues)
    if (c.startMs < until) previous = c;
}

bool Subtitler::Impl::save(const std::vector<Cue>& cues, std::string* errorCode) {
  storedCues.insert(storedCues.end(), cues.begin(), cues.end());
  std::stable_sort(storedCues.begin(), storedCues.end(), [](const Cue& a, const Cue& b) { return a.startMs < b.startMs; });
  if (!store) return true;
  StoredSubtitles s;
  s.mediaId = spec.mediaId;
  s.targetLanguage = spec.targetLanguage;
  s.audioLanguage = language;
  {
    std::lock_guard<std::mutex> lock(readyMutex);
    s.ranges = ready;
  }
  s.cues = storedCues;
  return store->save(s, errorCode);
}

Step Subtitler::step() {
  Impl& m = *impl_;
  const int64_t jump = m.pendingSeek.exchange(-1);
  if (jump >= 0) m.restartAt(jump);
  if (!m.waiting) m.skipReady();
  const uint64_t generation = m.generation;
  const Stale stale = [&m, generation] { return m.cancelled || m.generation != generation; };
  m.lastStage = Stage::kAudio;
  m.lastFraction = 0;
  m.report(Stage::kAudio, 0);
  if (m.waiting) {  // heard before the translator came: not heard again
    Impl::Waiting w = std::move(*m.waiting);
    m.waiting.reset();
    return m.finish(std::move(w.step), std::move(w.sentences), w.readyUntilMs, w.ended);
  }
  Step step;
  step.fromMs = m.nextFromMs;
  if (m.ended) {
    step.status = StepStatus::kEndOfMedia;
    step.readyUntilMs = m.nextFromMs;
    return step;
  }
  Listened heard = m.listen(stale);
  step.audioLanguage = m.language;
  if (heard.kind == Piece::Kind::kCancelled || stale()) {
    step.status = StepStatus::kCancelled;
    return step;
  }
  if (heard.kind == Piece::Kind::kError) {
    step.status = StepStatus::kError;
    step.errorCode = heard.error;
    return step;
  }
  std::vector<Sentence> sentences = joinSentences(m.cleaner.clean(std::move(heard.cues)), m.spec.config.join);
  return m.finish(std::move(step), std::move(sentences), heard.readyUntilMs, heard.kind == Piece::Kind::kEnd);
}

// Translates when the audio is not in the language asked for (asking for a
// translator when there is none), shapes the cues after the last one given
// out, and adds the stretch to what is ready.
Step Subtitler::Impl::finish(Step step, std::vector<Sentence> sentences, int64_t readyUntilMs, bool atEnd) {
  step.audioLanguage = language;
  const bool translate = !language.empty() && !sameLanguage(language, spec.targetLanguage);
  if (translate && !sentences.empty() && !translator) {
    step.status = StepStatus::kTranslatorNeeded;
    step.readyUntilMs = step.fromMs;
    waiting = Waiting{step, std::move(sentences), readyUntilMs, atEnd};
    return step;
  }
  report(Stage::kTranslating, 0);
  if (translate && !sentences.empty()) sentences = translateSentences(sentences, *translator);
  step.cues = formatter.format(previous, sentences);
  if (!step.cues.empty()) previous = step.cues.back();
  step.readyUntilMs = std::max(readyUntilMs, step.fromMs);
  step.status = atEnd ? StepStatus::kEndOfMedia : StepStatus::kCues;
  {
    std::lock_guard<std::mutex> lock(readyMutex);
    ready.add(step.fromMs, step.readyUntilMs);
  }
  nextFromMs = step.readyUntilMs;
  ended = atEnd;
  if (!save(step.cues, &step.errorCode)) step.status = StepStatus::kError;  // store_failed: the cues still go out
  feed->dropBefore(std::min(first.atMs, spec.config.second.enabled ? second.atMs : first.atMs));
  report(Stage::kTranslating, 1.0);
  return step;
}

}  // namespace subtitle_engine
