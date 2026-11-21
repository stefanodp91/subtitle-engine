#pragma once
// One translated sentence repaired against its source. The en->it model of
// Firefox writes a short sentence twice ("What?" -> "Cosa? Cosa?") and adds a
// dialogue dash ("No." -> "- No."); the translator hands back each source
// sentence with its translation, so the repair is made sentence by sentence.

#include <string>
#include <string_view>

namespace subtitle_engine {

// The target with one copy when it is the same group of sentences written two
// or more times and the source is not, and without a leading dialogue dash the
// source has not; anything else comes back as it is.
std::string repairSentence(std::string_view source, std::string_view target);

}  // namespace subtitle_engine
