#pragma once
// Text helpers for heard cues, on UTF-8: code points, words, normal forms.
// A word is a run of letters and apostrophes: the Latin letters of the
// feasibility probe ([A-Za-zÀ-ÿ']) and the letters of other alphabets.

#include <string>
#include <vector>

namespace subtitle_engine::cue_text {

std::u32string decode(const std::string& utf8);  // invalid bytes become U+FFFD
std::string encode(const std::u32string& text);

bool isLetter(char32_t c);      // a word's character: letters and the apostrophe
bool isWordChar(char32_t c);    // as \w: letters, digits and '_'
bool isSpace(char32_t c);
char32_t lower(char32_t c);

std::vector<std::u32string> words(const std::u32string& text);  // lower case
std::string norm(const std::string& text);                       // the words, lower case, one space between
std::u32string squeeze(const std::u32string& text);              // the runs of space as one space, trimmed
size_t length(const std::string& utf8);                           // in code points

}  // namespace subtitle_engine::cue_text
