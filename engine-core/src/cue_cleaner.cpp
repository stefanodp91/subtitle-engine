#include "cue_cleaner.hpp"

#include <algorithm>

#include "cue_text.hpp"

namespace subtitle_engine {

namespace ct = cue_text;

namespace {

// "(music)", "[door slams]", "♪ la la", "# la la": a sound or a song, no speech.
bool onlySound(const std::u32string& text) {
  const std::u32string t = ct::squeeze(text);
  if (t.empty()) return false;
  if ((t.front() == U'(' || t.front() == U'[') && (t.back() == U')' || t.back() == U']')) return true;
  return t.front() == U'♪' || t.front() == U'#';
}

// "*Gasps*": a sound written between asterisks, never a repetition worth keeping.
bool soundTag(const std::u32string& text) {
  const std::u32string t = ct::squeeze(text);
  return t.size() >= 3 && t.front() == U'*' && t.back() == U'*' &&
         std::count(t.begin() + 1, t.end() - 1, U'*') == 0;
}

std::string lowerAscii(std::string s) {
  for (char& c : s)
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
  return s;
}

bool isLoopSeparator(char32_t c) {
  return ct::isSpace(c) || c == U',' || c == U'.' || c == U'!' || c == U'?' || c == U'…' || c == U'-';
}

struct Token {
  size_t start, end;   // in code points
  std::u32string low;  // lower case
};

// The runs of \w characters with the apostrophes inside them, as the probe's loop pattern sees words.
std::vector<Token> loopTokens(const std::u32string& t) {
  std::vector<Token> out;
  for (size_t i = 0; i < t.size();) {
    if (!ct::isWordChar(t[i])) {
      ++i;
      continue;
    }
    size_t j = i;
    while (j < t.size() && (ct::isWordChar(t[j]) || (t[j] == U'\'' && j + 1 < t.size() && ct::isWordChar(t[j + 1]))))
      ++j;
    std::u32string low;
    for (size_t k = i; k < j; ++k) low += ct::lower(t[k]);
    out.push_back({i, j, low});
    i = j;
  }
  return out;
}

bool onlyOf(const std::u32string& t, size_t from, size_t to, bool (*allowed)(char32_t)) {
  if (from >= to) return false;
  for (size_t k = from; k < to; ++k)
    if (!allowed(t[k])) return false;
  return true;
}

bool isPhraseGap(char32_t c) { return c == U' '; }

}  // namespace

CueCleaner::CueCleaner(CleanupRules rules, std::string promptNames) : rules_(std::move(rules)) {
  for (auto& w : ct::words(ct::decode(promptNames)))
    if (static_cast<int>(w.size()) >= rules_.promptNameMinChars) prompt_.insert(w);
}

void CueCleaner::reset() {
  recent_.clear();
  previous_.clear();
  previousEnd_ = -1;
}

bool CueCleaner::isPromptOnly(const std::vector<std::u32string>& words) const {
  if (static_cast<int>(words.size()) < rules_.promptMinWords) return false;
  const auto names = std::count_if(words.begin(), words.end(), [&](const auto& w) { return prompt_.count(w) > 0; });
  return static_cast<double>(names) >= rules_.promptShare * static_cast<double>(words.size());
}

bool CueCleaner::seenLately(const Cue& cue) const {
  const std::string n = ct::norm(cue.text);
  const size_t from = recent_.size() > static_cast<size_t>(rules_.duplicateLookBack)
                          ? recent_.size() - static_cast<size_t>(rules_.duplicateLookBack)
                          : 0;
  for (size_t i = from; i < recent_.size(); ++i)
    if (recent_[i].second == n && cue.startMs - recent_[i].first <= rules_.duplicateWindow.count()) return true;
  return false;
}

// A phrase (up to loopPhraseChars, words joined by spaces) said loopMinRepeats
// times or more in a row, with only spaces and punctuation between, is kept
// loopKeep times, joined by ", ": the shortest phrase from the earliest start.
std::string CueCleaner::trimLoops(const std::string& text) const {
  const std::u32string t = ct::decode(text);
  const std::vector<Token> tok = loopTokens(t);
  std::u32string out;
  size_t copied = 0, i = 0;
  while (i < tok.size()) {
    bool trimmed = false;
    for (size_t k = 1; i + k <= tok.size(); ++k) {
      const size_t phraseEnd = tok[i + k - 1].end;
      if (phraseEnd - tok[i].start > static_cast<size_t>(rules_.loopPhraseChars)) break;
      if (k > 1 && !onlyOf(t, tok[i + k - 2].end, tok[i + k - 1].start, isPhraseGap)) break;
      size_t repeats = 1, j = i + k;
      while (j + k <= tok.size() && onlyOf(t, tok[j - 1].end, tok[j].start, isLoopSeparator)) {
        bool same = true;
        for (size_t q = 0; q < k && same; ++q)
          same = tok[j + q].low == tok[i + q].low && (q == 0 || onlyOf(t, tok[j + q - 1].end, tok[j + q].start, isPhraseGap));
        if (!same) break;
        ++repeats;
        j += k;
      }
      if (repeats >= static_cast<size_t>(rules_.loopMinRepeats)) {
        const std::u32string phrase = t.substr(tok[i].start, phraseEnd - tok[i].start);
        out += t.substr(copied, tok[i].start - copied);
        for (int r = 0; r < rules_.loopKeep; ++r) out += (r ? U", " : U"") + phrase;
        copied = tok[j - 1].end;
        i = j;
        trimmed = true;
        break;
      }
    }
    if (!trimmed) ++i;
  }
  out += t.substr(copied);
  return ct::encode(out);
}

std::vector<Cue> CueCleaner::clean(std::vector<Cue> heard) {
  std::vector<Cue> kept;
  for (Cue& cue : heard) {
    const std::u32string t = ct::decode(cue.text);
    const std::string n = ct::norm(cue.text);
    const bool repeat = !n.empty() && n == previous_ &&
                        (previousEnd_ < 0 || cue.startMs - previousEnd_ < rules_.repeatPause.count() || soundTag(t));
    if (onlySound(t) || n.empty() || repeat) {
      if (!n.empty()) {
        previous_ = n;
        previousEnd_ = cue.endMs;
      }
      continue;
    }
    const std::string low = lowerAscii(cue.text);
    if (std::any_of(rules_.creditPatterns.begin(), rules_.creditPatterns.end(),
                    [&](const std::string& p) { return low.find(lowerAscii(p)) != std::string::npos; }))
      continue;
    const auto words = ct::words(t);
    if (isPromptOnly(words)) continue;
    cue.text = trimLoops(cue.text);
    if (static_cast<int>(words.size()) >= rules_.duplicateMinWords && seenLately(cue)) continue;
    previous_ = ct::norm(cue.text);
    previousEnd_ = cue.endMs;
    recent_.emplace_back(cue.startMs, previous_);
    while (recent_.size() > static_cast<size_t>(rules_.duplicateLookBack)) recent_.pop_front();
    kept.push_back(std::move(cue));
  }
  return kept;
}

}  // namespace subtitle_engine
