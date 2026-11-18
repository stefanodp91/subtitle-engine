#pragma once
// The rules for heard cues: the cleaning, the sentences, the shape on screen.
// Every value has its name here; the defaults are those measured in the
// feasibility probe, and a caller can pass its own.

#include <chrono>
#include <string>
#include <vector>

namespace subtitle_engine {

struct CleanupRules {
  int loopMinRepeats = 5;                         // a word or phrase said this many times in a row...
  int loopKeep = 2;                               // ...is kept this many times
  int loopPhraseChars = 42;                       // the longest phrase a loop is looked for in
  std::chrono::milliseconds duplicateWindow{15000};
  int duplicateMinWords = 2;                      // the window rule only for texts of at least this many words
  int duplicateLookBack = 8;                      // the window rule looks at this many cues kept before
  std::chrono::milliseconds repeatPause{500};     // a text equal to the one before is kept when it starts
                                                  // this long after the other ended ("No! No!"); "*Gasps*" never
  double promptShare = 0.6;                       // a cue whose words are mostly the prompt's names...
  int promptMinWords = 3;                         // ...with at least this many words
  int promptNameMinChars = 3;                     // the prompt's words shorter than this are not names
  std::vector<std::string> creditPatterns{"sous-titr", "musik", "untertitel", "subtitles by", "amara.org", "s.t.o.s"};
};

struct JoinRules {
  std::chrono::milliseconds pause{1000};          // a sentence also ends at a pause this long between two cues
};

struct FormatRules {
  int lineChars = 42;
  int maxLines = 2;
  std::chrono::milliseconds minOnScreen{1000}, maxOnScreen{7000}, gap{83};
  std::chrono::milliseconds minLength{200};       // never shorter on screen, whatever the room
  std::chrono::milliseconds slack{10};            // within this a time counts as reached
  int cutMargin = 8;                              // a long text is never cut this close to either end
  int breakSlack = 8;                             // a line break moves this far from the middle to follow punctuation
};

}  // namespace subtitle_engine
