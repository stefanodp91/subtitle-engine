#include "subtitle_engine/ready_ranges.hpp"

#include <algorithm>

namespace subtitle_engine {

void ReadyRanges::add(int64_t fromMs, int64_t toMs) {
  if (toMs <= fromMs) return;
  spans_.emplace_back(fromMs, toMs);
  std::sort(spans_.begin(), spans_.end());
  std::vector<std::pair<int64_t, int64_t>> merged;
  for (const auto& s : spans_) {
    if (!merged.empty() && s.first <= merged.back().second)
      merged.back().second = std::max(merged.back().second, s.second);
    else
      merged.push_back(s);
  }
  spans_ = std::move(merged);
}

bool ReadyRanges::contains(int64_t positionMs) const {
  return std::any_of(spans_.begin(), spans_.end(),
                     [&](const auto& s) { return positionMs >= s.first && positionMs < s.second; });
}

int64_t ReadyRanges::endOf(int64_t positionMs) const {
  for (const auto& s : spans_)
    if (positionMs >= s.first && positionMs < s.second) return s.second;
  return positionMs;
}

int64_t ReadyRanges::nextStartAfter(int64_t positionMs) const {
  for (const auto& s : spans_)
    if (s.first > positionMs) return s.first;
  return -1;
}

}  // namespace subtitle_engine
