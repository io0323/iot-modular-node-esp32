// リセット要因の対応表（基本設計書 第6章、P6 の決定 #100・#112）。表の全行と「そのほか」。
#include <gtest/gtest.h>

#include <string_view>
#include <utility>

#include "infra/reset_cause.hpp"

namespace node::infra {
namespace {

using C = PlatformResetCause;

class ResetReasonTable : public ::testing::TestWithParam<std::pair<C, std::string_view>> {};

TEST_P(ResetReasonTable, MapsToSevenValues) {
  EXPECT_EQ(reset_reason_of(GetParam().first), GetParam().second);
}

INSTANTIATE_TEST_SUITE_P(DesignChapter6, ResetReasonTable,
                         ::testing::Values(std::pair{C::PowerOn, std::string_view{"power_on"}},
                                           std::pair{C::External, std::string_view{"pin"}},
                                           std::pair{C::Software, std::string_view{"software"}},
                                           std::pair{C::Panic, std::string_view{"software"}},
                                           std::pair{C::InterruptWatchdog, std::string_view{"watchdog"}},
                                           std::pair{C::TaskWatchdog, std::string_view{"watchdog"}},
                                           std::pair{C::OtherWatchdog, std::string_view{"watchdog"}},
                                           std::pair{C::Brownout, std::string_view{"brownout"}},
                                           std::pair{C::PowerGlitch, std::string_view{"brownout"}},
                                           std::pair{C::DeepSleep, std::string_view{"low_power"}},
                                           std::pair{C::Unknown, std::string_view{"unknown"}}));

TEST(ResetReason, OutOfRangeValueIsUnknown) {
  EXPECT_EQ(reset_reason_of(static_cast<C>(200)), "unknown");
}

TEST(ResetReason, IsConstexpr) {
  static_assert(reset_reason_of(C::TaskWatchdog) == "watchdog");
  SUCCEED();
}

}  // namespace
}  // namespace node::infra
