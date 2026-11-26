#pragma once
// The stretches of the film already heard, merged when they touch.

#include <cstdint>
#include <utility>
#include <vector>

namespace subtitle_engine {

class ReadyRanges {
 public:
  void add(int64_t fromMs, int64_t toMs);
  bool contains(int64_t positionMs) const;
  int64_t endOf(int64_t positionMs) const;  // the end of the stretch holding positionMs, or positionMs
  int64_t nextStartAfter(int64_t positionMs) const;  // the start of the first stretch after positionMs, or -1
  const std::vector<std::pair<int64_t, int64_t>>& spans() const { return spans_; }

 private:
  std::vector<std::pair<int64_t, int64_t>> spans_;  // sorted, apart from each other
};

}  // namespace subtitle_engine
