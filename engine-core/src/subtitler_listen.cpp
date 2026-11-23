// The listening side of the queue: where a piece ends, hearing it, and the
// double listen (DEC-886) put together stretch by stretch.

#include <algorithm>

#include "dense_cue.hpp"
#include "listen_merge.hpp"
#include "piece_cutter.hpp"
#include "subtitler_impl.hpp"

namespace subtitle_engine {

namespace {

std::vector<Cue> plain(const std::vector<HeardCue>& cues) {
  std::vector<Cue> out;
  for (const auto& c : cues) out.push_back(c.cue);
  std::stable_sort(out.begin(), out.end(), [](const Cue& a, const Cue& b) { return a.startMs < b.startMs; });
  return out;
}

}  // namespace

bool Subtitler::Impl::keeps(const std::vector<Cue>& cues) const {
  CueCleaner copy = cleaner;  // a copy: asking must not change what the cleaner remembers
  return !copy.clean(cues).empty();
}

// Waits until the audio from cursor holds a piece: cut at a pause by the rules,
// or what arrived when the download stands still for stall, or the rest at the end.
// A piece starts where the audio does, so its samples keep the film's time.
Piece Subtitler::Impl::waitPiece(Cursor& cursor, const PieceRules& rules, const Stale& stale) {
  const auto& cfg = spec.config;
  Piece piece;
  for (;;) {
    if (stale()) {
      piece.kind = Piece::Kind::kCancelled;
      return piece;
    }
    const AudioFeed::State st = feed->state();
    if (!st.error.empty()) {
      piece.kind = Piece::Kind::kError;
      piece.error = st.error;
      return piece;
    }
    if (st.startMs > cursor.atMs) {  // the audio starts later (a jump into a stretch the file has not): none before it
      cursor.atMs = st.startMs;
      cursor.lastProbeMs = -1;
    }
    const int64_t buffered = std::max<int64_t>(0, st.endMs - cursor.atMs);
    const int64_t limit = std::min<int64_t>(rules.piece.count(), rules.firstPiece.count() * (cursor.sinceRamp + 1));
    auto speechTo = [&](int64_t untilMs) {
      const auto samples = feed->samples(cursor.atMs, untilMs);
      return recognizer.speech(samples.data(), samples.size(), cursor.atMs);
    };
    std::optional<int64_t> cut;
    bool stalled = false;
    if (cursor.sinceRamp == 0)  // BUG-311: at fixed points of the piece, whenever the audio came
      cut = firstCutPoint(rules, buffered, &cursor.lastProbeMs,
                          [&](int64_t untilMs) { return speechTo(cursor.atMs + untilMs); });
    if (!cut && st.ended) {
      cut = cutPoint(rules, buffered, speechTo(cursor.atMs + std::min(buffered, limit)), cursor.sinceRamp, true);
    } else if (!cut && buffered >= limit) {
      cut = cutPoint(rules, buffered, speechTo(cursor.atMs + limit), cursor.sinceRamp, false);
    }
    if (!cut && st.idle >= cfg.stall && buffered >= rules.minTail.count()) {
      cut = buffered;  // the download stands still: hear what arrived
      stalled = true;
    }
    if (cut && *cut > 0) {
      piece.kind = Piece::Kind::kAudio;
      piece.atMs = cursor.atMs;
      piece.endMs = cursor.atMs + *cut;
      piece.samples = feed->samples(piece.atMs, piece.endMs);
      piece.ramp = cursor.sinceRamp == 0;
      cursor.atMs = piece.endMs;
      cursor.sinceRamp = stalled ? 0 : cursor.sinceRamp + 1;
      cursor.lastProbeMs = -1;
      return piece;
    }
    if (st.ended) {
      piece.kind = Piece::Kind::kEnd;
      piece.atMs = piece.endMs = st.endMs;
      return piece;
    }
    report(Stage::kAudio, limit > 0 ? static_cast<double>(buffered) / static_cast<double>(limit) : 0);
    feed->waitForChange(cfg.waitPoll);
  }
}

// Hears a piece and adds its cues; false when the step went stale. The language
// is fixed by the track or by the first piece with speech: whisper's words and
// the voice detector's too, since on music whisper may write a line of one
// repeated letter in a language it made up. Second tries at a higher
// temperature only for the retry languages and never on the first piece.
bool Subtitler::Impl::hearInto(const Piece& piece, std::vector<HeardCue>* into, const Stale& stale) {
  const auto& cfg = spec.config;
  HearOptions options;
  options.language = language;
  options.prompt = spec.prompt;
  const bool retry = std::find(cfg.retryLanguages.begin(), cfg.retryLanguages.end(), language) != cfg.retryLanguages.end();
  options.temperatureInc = heardPieces > 0 && !language.empty() && retry ? cfg.temperatureInc : 0.0f;
  options.cancelled = stale;
  Heard heard = recognizer.hear(piece.samples.data(), piece.samples.size(), piece.atMs, options);
  ++heardPieces;
  if (heard.cancelled || stale()) return false;
  if (language.empty() && heard.spoken && !recognizer.speech(piece.samples.data(), piece.samples.size(), piece.atMs).empty())
    language = heard.detectedLanguage;
  into->insert(into->end(), heard.cues.begin(), heard.cues.end());
  return true;
}

// BUG-307: when whisper packed a cue of the piece just heard denser than speech,
// the piece ends at that cue and the cursor goes back there: the cue and the ones
// after it are heard again by the next piece.
void Subtitler::Impl::rehearDense(Piece& piece, std::vector<HeardCue>* cues, size_t heardFrom, Cursor& cursor) {
  const auto& cfg = spec.config;
  const std::vector<HeardCue> heard(cues->begin() + static_cast<long>(heardFrom), cues->end());
  const auto at = rehearFrom(heard, piece.atMs, cfg.rehear, cfg.pieces.minTail, cfg.second.loopRatio);
  if (!at) return;
  cues->erase(std::remove_if(cues->begin() + static_cast<long>(heardFrom), cues->end(),
                             [&](const HeardCue& c) { return c.cue.startMs >= *at; }),
              cues->end());
  piece.endMs = *at;
  cursor.atMs = *at;
  cursor.lastProbeMs = -1;
}

// One piece of the first listen and, with the double listen, the second
// listen's pieces up to its end; the stretches finished within the audio both
// listens covered are final, the others wait for the next step.
Listened Subtitler::Impl::listen(const Stale& stale) {
  const auto& cfg = spec.config;
  Listened out;
  Piece p1 = waitPiece(first, cfg.pieces, stale);
  if (p1.kind == Piece::Kind::kCancelled || p1.kind == Piece::Kind::kError) {
    out.kind = p1.kind;
    out.error = p1.error;
    return out;
  }
  const bool twice = cfg.second.enabled;
  if (p1.kind == Piece::Kind::kEnd) {  // nothing left to hear: everything still waiting is final
    out.kind = Piece::Kind::kEnd;
    out.cues = plain(twice ? mergeListens(carryFirst, carrySecond, cfg.second, [&](const auto& c) { return keeps(c); })
                           : carryFirst);
    carryFirst.clear();
    carrySecond.clear();
    out.readyUntilMs = p1.endMs;
    return out;
  }
  report(Stage::kListening, 0);
  const size_t firstFrom = carryFirst.size();
  if (!hearInto(p1, &carryFirst, stale)) {
    out.kind = Piece::Kind::kCancelled;
    return out;
  }
  rehearDense(p1, &carryFirst, firstFrom, first);
  out.readyUntilMs = p1.endMs;
  if (!twice) {
    out.cues = plain(carryFirst);
    carryFirst.clear();
    return out;
  }
  if (p1.ramp) {  // the first piece after the start, a jump or a stall is heard once (DEC-841)
    second = {p1.endMs, 1, -1};
  } else {
    PieceRules rules = cfg.pieces;
    rules.piece = cfg.second.secondPiece;
    while (second.atMs < p1.endMs) {
      Piece p2 = waitPiece(second, rules, stale);
      if (p2.kind == Piece::Kind::kCancelled || p2.kind == Piece::Kind::kError) {
        out.kind = p2.kind;
        out.error = p2.error;
        return out;
      }
      if (p2.kind == Piece::Kind::kEnd) break;
      report(Stage::kListening, 0.5);
      const size_t secondFrom = carrySecond.size();
      if (!hearInto(p2, &carrySecond, stale)) {
        out.kind = Piece::Kind::kCancelled;
        return out;
      }
      rehearDense(p2, &carrySecond, secondFrom, second);
    }
  }
  std::vector<HeardCue> merged, keepFirst, keepSecond;
  const auto keep = [&](const std::vector<Cue>& c) { return keeps(c); };
  for (const auto& s : listenStretches(carryFirst, carrySecond)) {
    if (s.endMs <= p1.endMs) {
      auto cues = mergeStretch(s, cfg.second, keep);
      merged.insert(merged.end(), cues.begin(), cues.end());
    } else {
      out.readyUntilMs = std::min(out.readyUntilMs, s.startMs);
      keepFirst.insert(keepFirst.end(), s.first.begin(), s.first.end());
      keepSecond.insert(keepSecond.end(), s.second.begin(), s.second.end());
    }
  }
  carryFirst = std::move(keepFirst);
  carrySecond = std::move(keepSecond);
  out.cues = plain(merged);
  return out;
}

}  // namespace subtitle_engine
