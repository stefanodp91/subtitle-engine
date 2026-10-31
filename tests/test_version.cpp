#include <gtest/gtest.h>

#include "subtitle_engine_c.h"

TEST(Version, ReportsSemver) { EXPECT_STREQ(se_version(), "0.1.0"); }
