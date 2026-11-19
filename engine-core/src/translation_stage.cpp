#include "translation_stage.hpp"

#include <algorithm>
#include <cctype>

#include "whisper.h"

namespace subtitle_engine {

namespace {

std::string primary(std::string tag) {
  tag = tag.substr(0, tag.find_first_of("-_"));
  std::transform(tag.begin(), tag.end(), tag.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return tag;
}

}  // namespace

std::string whisperCode(const std::string& tag) {
  static const char* const kIso[][2] = {
      {"ita", "it"}, {"eng", "en"}, {"fre", "fr"}, {"fra", "fr"}, {"ger", "de"}, {"deu", "de"}, {"spa", "es"},
      {"por", "pt"}, {"jpn", "ja"}, {"kor", "ko"}, {"chi", "zh"}, {"zho", "zh"}, {"rus", "ru"}, {"dut", "nl"},
      {"nld", "nl"}, {"pol", "pl"}, {"tur", "tr"}, {"ara", "ar"}, {"hin", "hi"}, {"swe", "sv"}, {"dan", "da"},
      {"nor", "no"}, {"fin", "fi"}, {"gre", "el"}, {"ell", "el"}, {"heb", "he"}, {"cze", "cs"}, {"ces", "cs"},
      {"hun", "hu"}, {"rum", "ro"}, {"ron", "ro"}, {"ukr", "uk"}, {"tha", "th"}, {"vie", "vi"}, {"ind", "id"}};
  const std::string t = primary(tag);
  if (t.empty() || t == "und") return "";
  for (const auto& e : kIso)
    if (t == e[0]) return e[1];
  if (t.size() == 2 && whisper_lang_id(t.c_str()) >= 0) return whisper_lang_str(whisper_lang_id(t.c_str()));
  return "";
}

bool sameLanguage(const std::string& whisper, const std::string& bcp47) { return primary(whisper) == primary(bcp47); }

std::vector<Sentence> translateSentences(const std::vector<Sentence>& sentences, TextTranslator& translator) {
  if (sentences.empty()) return sentences;
  std::vector<std::string> lines;
  for (const auto& s : sentences) lines.push_back(s.text);
  const std::vector<std::string> out = translator.translate(lines);
  std::vector<Sentence> translated = sentences;
  for (size_t i = 0; i < translated.size() && i < out.size(); ++i) translated[i].text = out[i];
  return translated;
}

}  // namespace subtitle_engine
