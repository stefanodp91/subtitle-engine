#include "sentence_repair.hpp"

#include <cstddef>
#include <vector>

namespace subtitle_engine {

namespace {

constexpr std::string_view kDialogueDash = "- ";
constexpr std::string_view kEllipsis = "\xE2\x80\xA6";  // U+2026

struct Span {
  size_t begin = 0;
  size_t end = 0;
};

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

bool endsSentence(std::string_view text, size_t end) {
  const char c = text[end - 1];
  if (c == '.' || c == '?' || c == '!') return true;
  return end >= kEllipsis.size() && text.substr(end - kEllipsis.size(), kEllipsis.size()) == kEllipsis;
}

// The sentences of text, cut at the spaces after a sentence end.
std::vector<Span> sentences(std::string_view text) {
  std::vector<Span> out;
  size_t at = 0;
  while (at < text.size()) {
    while (at < text.size() && isSpace(text[at])) ++at;
    if (at == text.size()) break;
    size_t end = at;
    while (end < text.size() && !(isSpace(text[end]) && endsSentence(text, end))) ++end;
    size_t last = end;
    while (last > at && isSpace(text[last - 1])) --last;
    out.push_back({at, last});
    at = end;
  }
  return out;
}

// The words of a sentence, lowercased, without punctuation: "Sì?" and "sì."
// compare equal. Bytes of UTF-8 letters count as letters; the Latin-1 capitals
// are lowered; quotes, dashes and the ellipsis (U+0080-U+00BF, U+2000-U+206F)
// are punctuation.
std::string words(std::string_view sentence) {
  std::string out;
  bool gap = false;
  for (size_t i = 0; i < sentence.size(); ++i) {
    const auto c = static_cast<unsigned char>(sentence[i]);
    size_t length = 1;
    std::string letter;
    if (c < 0x80) {
      if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '\'') letter = static_cast<char>(c);
      if (c >= 'A' && c <= 'Z') letter = static_cast<char>(c - 'A' + 'a');
    } else {
      length = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : 2;
      const std::string_view sequence = sentence.substr(i, length);
      const auto next = sequence.size() > 1 ? static_cast<unsigned char>(sequence[1]) : 0;
      const bool punctuation = (c == 0xC2) || (c == 0xE2 && next == 0x80) || (c == 0xE2 && next == 0x81);
      if (!punctuation) {
        letter = std::string(sequence);
        if (c == 0xC3 && next >= 0x80 && next <= 0x9E && next != 0x97) letter[1] = static_cast<char>(next + 0x20);
      }
    }
    if (letter.empty()) {
      gap = !out.empty();
    } else {
      if (gap) out += ' ';
      out += letter;
      gap = false;
    }
    i += length - 1;
  }
  return out;
}

// How many leading sentences make the whole text when written again and
// again; parts.size() when no shorter group does.
size_t period(std::string_view text, const std::vector<Span>& parts) {
  std::vector<std::string> said;
  for (const auto& p : parts) said.push_back(words(text.substr(p.begin, p.end - p.begin)));
  for (size_t length = 1; length <= parts.size() / 2; ++length) {
    if (parts.size() % length != 0) continue;
    bool repeats = true;
    for (size_t i = length; i < parts.size() && repeats; ++i) repeats = !said[i].empty() && said[i] == said[i % length];
    if (repeats) return length;
  }
  return parts.size();
}

bool isRepeated(std::string_view text) {
  const auto parts = sentences(text);
  return period(text, parts) < parts.size();
}

size_t count(std::string_view text, std::string_view what) {
  size_t n = 0;
  for (size_t at = text.find(what); at != std::string_view::npos; at = text.find(what, at + what.size())) ++n;
  return n;
}

std::string_view trimmedFront(std::string_view text) {
  while (!text.empty() && isSpace(text.front())) text.remove_prefix(1);
  return text;
}

}  // namespace

std::string repairSentence(std::string_view source, std::string_view target) {
  std::string_view out = target;
  const auto parts = sentences(target);
  const size_t length = period(target, parts);
  if (length < parts.size() && !isRepeated(source)) {
    out = target.substr(parts.front().begin, parts[length - 1].end - parts.front().begin);
  }
  if (out.substr(0, kDialogueDash.size()) == kDialogueDash && count(out, kDialogueDash) == 1 &&
      trimmedFront(source).substr(0, 1) != "-") {
    out.remove_prefix(kDialogueDash.size());
  }
  return std::string(out);
}

}  // namespace subtitle_engine
