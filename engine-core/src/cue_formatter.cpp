#include "cue_formatter.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "cue_text.hpp"

namespace subtitle_engine {

namespace ct = cue_text;

namespace {

bool isStop(char32_t c) { return c == U'.' || c == U'?' || c == U'!' || c == U'…'; }
bool isComma(char32_t c) { return c == U',' || c == U';' || c == U':'; }
bool isBreakMark(char32_t c) { return isComma(c) || isStop(c); }

std::u32string trim(const std::u32string& t) {
  size_t a = 0, b = t.size();
  while (a < b && ct::isSpace(t[a])) ++a;
  while (b > a && ct::isSpace(t[b - 1])) --b;
  return t.substr(a, b - a);
}

double distance(double a, double b) { return std::fabs(a - b); }

struct Piece {
  double start, end;
  std::u32string text;
};

}  // namespace

CueFormatter::CueFormatter(FormatRules rules) : rules_(std::move(rules)) {}

// The text in pieces of at most maxLines lines: cut at a sentence end, else at
// a comma, when it is within a quarter of the length from the middle; else at
// the space nearest the middle; never closer than cutMargin to either end.
std::vector<std::u32string> CueFormatter::split(const std::u32string& t) const {
  const size_t fits = static_cast<size_t>(rules_.lineChars * rules_.maxLines);
  if (t.size() <= fits) return {t};
  const double mid = static_cast<double>(t.size()) / 2;
  const size_t margin = static_cast<size_t>(rules_.cutMargin);
  for (int pass = 0; pass < 3; ++pass) {
    std::vector<size_t> cuts;
    for (size_t i = 0; i < t.size(); ++i) {
      if (!ct::isSpace(t[i])) continue;
      size_t cut = i + 1;  // after the space
      if (pass < 2) {
        if (i == 0 || !(pass == 0 ? isStop(t[i - 1]) : isComma(t[i - 1]))) continue;
        if (cut < margin || cut > t.size() - margin) continue;
        if (distance(static_cast<double>(cut), mid) > static_cast<double>(t.size()) / 4) continue;
      } else if (cut < margin || cut > t.size() - margin) {
        continue;
      }
      cuts.push_back(cut);
    }
    if (cuts.empty()) continue;
    const size_t c = *std::min_element(cuts.begin(), cuts.end(), [&](size_t a, size_t b) {
      return distance(static_cast<double>(a), mid) < distance(static_cast<double>(b), mid);
    });
    std::vector<std::u32string> out = split(trim(t.substr(0, c)));
    const std::vector<std::u32string> rest = split(trim(t.substr(c)));
    out.insert(out.end(), rest.begin(), rest.end());
    return out;
  }
  return {t};
}

// Two balanced lines when the text is longer than a line, broken after a
// punctuation mark when that costs at most breakSlack characters of balance.
std::u32string CueFormatter::wrap(const std::u32string& t) const {
  const size_t line = static_cast<size_t>(rules_.lineChars);
  if (t.size() <= line) return t;
  std::vector<size_t> spaces, fits;
  for (size_t i = 0; i < t.size(); ++i)
    if (ct::isSpace(t[i])) spaces.push_back(i);
  for (size_t i : spaces)
    if (i <= line && t.size() - i - 1 <= line) fits.push_back(i);
  if (fits.empty()) fits = spaces;
  if (fits.empty()) return t;
  const double mid = static_cast<double>(t.size()) / 2;
  auto nearest = [&](const std::vector<size_t>& v) {
    return *std::min_element(v.begin(), v.end(), [&](size_t a, size_t b) {
      return distance(static_cast<double>(a), mid) < distance(static_cast<double>(b), mid);
    });
  };
  size_t best = nearest(fits);
  std::vector<size_t> punct;
  for (size_t i : fits)
    if (i > 0 && isBreakMark(t[i - 1])) punct.push_back(i);
  if (!punct.empty()) {
    const size_t p = nearest(punct);
    if (distance(static_cast<double>(p), mid) <= distance(static_cast<double>(best), mid) + rules_.breakSlack) best = p;
  }
  return t.substr(0, best) + U'\n' + t.substr(best + 1);
}

std::vector<Cue> CueFormatter::format(const std::optional<Cue>& previous, const std::vector<Sentence>& sentences) const {
  const double minOn = static_cast<double>(rules_.minOnScreen.count());
  const double maxOn = static_cast<double>(rules_.maxOnScreen.count());
  const double gap = static_cast<double>(rules_.gap.count());
  const double slack = static_cast<double>(rules_.slack.count());
  const size_t twoLines = static_cast<size_t>(rules_.lineChars * rules_.maxLines);
  std::vector<Piece> pieces;
  for (const Sentence& s : sentences) {
    const std::vector<std::u32string> parts = split(ct::squeeze(ct::decode(s.text)));
    size_t total = 0;
    for (const auto& p : parts) total += p.size();
    double at = static_cast<double>(s.startMs);
    for (const auto& p : parts) {
      const double d = static_cast<double>(s.endMs - s.startMs) * static_cast<double>(p.size()) /
                       static_cast<double>(std::max<size_t>(1, total));
      pieces.push_back({at, at + d, p});
      at += d;
    }
  }
  if (previous && !pieces.empty()) {  // never over the cue already given out
    const double earliest = static_cast<double>(previous->endMs) + gap;
    if (pieces.front().start < earliest) {
      pieces.front().start = earliest;
      pieces.front().end = std::max(pieces.front().end, earliest);
    }
  }
  // A cue shorter than minOnScreen takes the time from the start of the next
  // one when the next keeps minOnScreen, else joins it when both fit.
  for (size_t i = 0; i < pieces.size();) {
    double s = pieces[i].start, e = std::min(pieces[i].end, s + maxOn);
    if (i + 1 < pieces.size()) {
      Piece& n = pieces[i + 1];
      e = e - s < minOn ? std::min(std::max(e, s + minOn), n.start - gap) : std::min(e, n.start - gap);
      if (e - s < minOn - slack) {
        const double need = s + minOn + gap - n.start;
        if (n.end - (n.start + need) >= minOn) {
          n.start += need;
          e = s + minOn;
        } else if (pieces[i].text.size() + 1 + n.text.size() <= twoLines) {
          n = {s, n.end, pieces[i].text + U' ' + n.text};
          pieces.erase(pieces.begin() + static_cast<long>(i));
          continue;
        }
      }
    } else {
      e = std::max(e, s + minOn);
    }
    pieces[i].end = std::max(e, s + static_cast<double>(rules_.minLength.count()));
    ++i;
  }
  std::vector<Cue> out;
  for (const Piece& p : pieces) out.push_back({std::llround(p.start), std::llround(p.end), ct::encode(wrap(p.text))});
  return out;
}

}  // namespace subtitle_engine
