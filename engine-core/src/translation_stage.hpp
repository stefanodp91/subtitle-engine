#pragma once
// The languages of the queue and the translation of its sentences.

#include <string>
#include <vector>

#include "subtitle_engine/cue.hpp"
#include "subtitle_engine/text_translator.hpp"

namespace subtitle_engine {

// whisper's code of a track's language tag (ISO 639-2, both forms, or a code
// whisper knows already); empty for "und", no tag or one whisper does not know.
std::string whisperCode(const std::string& tag);

// Whether whisper's language and a BCP 47 tag have the same primary language.
bool sameLanguage(const std::string& whisper, const std::string& bcp47);

// The sentences with their text translated, one line each, same count and order.
std::vector<Sentence> translateSentences(const std::vector<Sentence>& sentences, TextTranslator& translator);

}  // namespace subtitle_engine
