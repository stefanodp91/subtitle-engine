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

// How the audio is cut into pieces for the listening (DEC-840, DEC-841):
// pieces end in a pause, the first ones after the start or a jump shorter.
struct PieceRules {
  std::chrono::milliseconds piece{20000}, cutMin{7000};          // DEC-840
  std::chrono::milliseconds firstPiece{8000}, firstCut{1500};    // DEC-841
  std::chrono::milliseconds firstPauseMin{150}, firstPauseOpen{300}, pauseMin{200}, minTail{500};
  std::chrono::milliseconds firstProbeEvery{500};  // the first piece looks for its pause again after this much new audio
};

// BUG-289: a cue starts at its first word, by whisper's alignment heads.
struct TimingRules {
  std::chrono::milliseconds endPad{500};           // a cue moved whole ends this long after its last word...
  std::chrono::milliseconds movedMin{1000};        // ...and lasts at least this long
  std::chrono::milliseconds startBeforeEnd{200};   // a start never closer to the cue's end than this
};

// DEC-886: the audio heard twice, cut in different places, chosen stretch by stretch.
struct DoubleListenRules {
  bool enabled = true;
  std::chrono::milliseconds secondPiece{14000};    // the second listen's pieces (the first's are PieceRules::piece)
  double margin = 0.0;                             // the second listen wins a stretch when surer by more than this
  double fillSure = 0.6;                           // a stretch only the second listen heard, taken from this confidence
  double loopRatio = 2.4;                          // a text whose zlib compression ratio is above this is a loop
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
