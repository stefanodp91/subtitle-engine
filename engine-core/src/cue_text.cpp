#include "cue_text.hpp"

namespace subtitle_engine::cue_text {

namespace {

// Punctuation and symbol blocks above Latin-1 that are not letters.
bool isPunctuationBlock(char32_t c) {
  return (c >= 0x2000 && c <= 0x2BFF) ||  // general punctuation, symbols, arrows, ♪…
         (c >= 0x2E00 && c <= 0x2E7F) ||  // supplemental punctuation
         (c >= 0x3000 && c <= 0x303F) ||  // CJK punctuation
         (c >= 0xFE10 && c <= 0xFE1F) || (c >= 0xFE30 && c <= 0xFE4F) ||
         (c >= 0xFF00 && c <= 0xFF0F) || (c >= 0xFF1A && c <= 0xFF20) || (c >= 0xFF3B && c <= 0xFF40) ||
         (c >= 0xFF5B && c <= 0xFF65) || c == 0xFEFF || c == 0xFFFD;
}

}  // namespace

std::u32string decode(const std::string& utf8) {
  std::u32string out;
  size_t i = 0;
  while (i < utf8.size()) {
    const auto b = static_cast<unsigned char>(utf8[i]);
    if (b < 0x80) {
      out += static_cast<char32_t>(b);
      ++i;
      continue;
    }
    int n = 0;
    char32_t c = 0;
    if ((b >> 5) == 0x6) { n = 1; c = b & 0x1F; }
    else if ((b >> 4) == 0xE) { n = 2; c = b & 0x0F; }
    else if ((b >> 3) == 0x1E) { n = 3; c = b & 0x07; }
    bool ok = n > 0 && i + n < utf8.size();
    for (int k = 1; ok && k <= n; ++k) {
      const auto cb = static_cast<unsigned char>(utf8[i + k]);
      ok = (cb & 0xC0) == 0x80;
      c = (c << 6) | (cb & 0x3F);
    }
    if (!ok) {
      out += static_cast<char32_t>(0xFFFD);
      ++i;
      continue;
    }
    out += c;
    i += static_cast<size_t>(n) + 1;
  }
  return out;
}

std::string encode(const std::u32string& text) {
  std::string out;
  for (char32_t c : text) {
    if (c < 0x80) {
      out += static_cast<char>(c);
    } else if (c < 0x800) {
      out += static_cast<char>(0xC0 | (c >> 6));
      out += static_cast<char>(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
      out += static_cast<char>(0xE0 | (c >> 12));
      out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (c & 0x3F));
    } else {
      out += static_cast<char>(0xF0 | (c >> 18));
      out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
      out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (c & 0x3F));
    }
  }
  return out;
}

bool isLetter(char32_t c) {
  if (c < 0x80) return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '\'';
  if (c < 0x100) return c >= 0xC0;  // À-ÿ, as the probe
  return !isPunctuationBlock(c);
}

bool isWordChar(char32_t c) { return (isLetter(c) && c != '\'') || (c >= '0' && c <= '9') || c == '_'; }

bool isSpace(char32_t c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f' || c == 0xA0 ||
         (c >= 0x2000 && c <= 0x200A) || c == 0x3000;
}

char32_t lower(char32_t c) {
  if (c >= 'A' && c <= 'Z') return c + 32;
  if (c >= 0xC0 && c <= 0xDE && c != 0xD7) return c + 32;
  return c;
}

std::vector<std::u32string> words(const std::u32string& text) {
  std::vector<std::u32string> out;
  std::u32string cur;
  for (char32_t c : text) {
    if (isLetter(c)) {
      cur += lower(c);
    } else if (!cur.empty()) {
      out.push_back(cur);
      cur.clear();
    }
  }
  if (!cur.empty()) out.push_back(cur);
  return out;
}

std::string norm(const std::string& text) {
  std::u32string out;
  for (const auto& w : words(decode(text))) {
    if (!out.empty()) out += U' ';
    out += w;
  }
  return encode(out);
}

std::u32string squeeze(const std::u32string& text) {
  std::u32string out;
  for (char32_t c : text) {
    if (isSpace(c)) {
      if (!out.empty() && out.back() != U' ') out += U' ';
    } else {
      out += c;
    }
  }
  while (!out.empty() && out.back() == U' ') out.pop_back();
  return out;
}

size_t length(const std::string& utf8) { return decode(utf8).size(); }

}  // namespace subtitle_engine::cue_text
