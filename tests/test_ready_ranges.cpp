#include <gtest/gtest.h>

#include "subtitle_engine/ready_ranges.hpp"

using namespace subtitle_engine;

TEST(ReadyRanges, MergesTouching) {
  ReadyRanges ranges;
  ranges.add(0, 10000);
  ranges.add(10000, 20000);
  EXPECT_EQ(ranges.spans().size(), 1u);
  ranges.add(30000, 40000);
  EXPECT_EQ(ranges.spans().size(), 2u);
  EXPECT_EQ(ranges.endOf(15000), 20000);
  EXPECT_FALSE(ranges.contains(25000));
  EXPECT_TRUE(ranges.contains(35000));
  EXPECT_EQ(ranges.endOf(25000), 25000);
}
