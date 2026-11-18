#include "piece_cutter.hpp"

#include <algorithm>

namespace subtitle_engine {

std::optional<int64_t> cutPoint(const PieceRules& rules, int64_t bufferedMs, const std::vector<SpeechSpan>& speech,
                                int pieceSinceRamp, bool endOfAudio) {
  const int64_t piece = std::min<int64_t>(rules.piece.count(), rules.firstPiece.count() * (pieceSinceRamp + 1));
  if (pieceSinceRamp == 0 && bufferedMs < piece &&
      bufferedMs >= rules.firstCut.count() + rules.firstPauseOpen.count()) {
    // The first piece: the first pause after firstCut, as soon as the audio has it;
    // a pause still open at the end of the audio counts once it is firstPauseOpen long.
    for (size_t i = 0; i < speech.size(); ++i) {
      const int64_t g0 = speech[i].endMs, g1 = i + 1 < speech.size() ? speech[i + 1].startMs : bufferedMs;
      const bool closed = i + 1 < speech.size() || bufferedMs - g0 >= rules.firstPauseOpen.count();
      const int64_t mid = (g0 + g1) / 2;
      if (closed && g1 - g0 >= rules.firstPauseMin.count() && mid >= rules.firstCut.count())
        return std::min<int64_t>(mid, g0 + rules.firstPauseOpen.count());
    }
  }
  if (bufferedMs >= piece) {
    // The last pause after min(cutMin, piece / 2), within the piece.
    const int64_t cutMin = std::min<int64_t>(rules.cutMin.count(), piece / 2);
    int64_t best = -1;
    for (size_t i = 0; i < speech.size() && speech[i].startMs < piece; ++i) {
      const int64_t g0 = speech[i].endMs;
      const int64_t g1 = i + 1 < speech.size() && speech[i + 1].startMs < piece ? speech[i + 1].startMs : piece;
      const int64_t mid = (g0 + g1) / 2;
      if (g1 - g0 >= rules.pauseMin.count() && mid >= cutMin && mid < piece) best = mid;
    }
    return best > 0 ? best : piece;
  }
  if (endOfAudio) return bufferedMs >= rules.minTail.count() ? bufferedMs : 0;
  return std::nullopt;
}

}  // namespace subtitle_engine
