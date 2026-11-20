#pragma once
// What the queue asks of a translator: lines in, lines out, same count and order.

#include <string>
#include <vector>

namespace subtitle_engine {

class TextTranslator {
 public:
  virtual ~TextTranslator() = default;
  virtual std::vector<std::string> translate(const std::vector<std::string>& lines) = 0;
};

}  // namespace subtitle_engine
