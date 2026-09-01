// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

/// Which years a farm can be run for, which is arithmetic at the ends of a file.
///
/// **A farm year is not a calendar year.** It runs 1 July to 30 June, so the
/// years a ten-year weather file can answer for are not the ten years its dates
/// span, and the mistake at either end is invisible until a run asks for
/// weather that is not there. This is the sum behind the window's "Every year"
/// button, and it is here rather than in a slot because a slot is not a thing a
/// test can call.

#include <gtest/gtest.h>

#include <optional>
#include <vector>

#include <paddock/core/SimulationClock.hpp>
#include <paddock/core/Weather.hpp>

#include "../../app/src/WholeYears.hpp"

namespace paddock::app {
namespace {

/// A source that covers whatever it is told to and produces nothing, because
/// nothing here ever fetches a day.
class Covering : public core::WeatherSource {
 public:
  explicit Covering(std::optional<core::DateRange> range) : range_(range) {}

  [[nodiscard]] core::SourceDescription describe() const override { return {}; }

  [[nodiscard]] core::ConnectionStatus test_connection() const override { return {}; }

  [[nodiscard]] core::WeatherSeries fetch(const core::DateRange&) const override { return {}; }

  [[nodiscard]] std::optional<core::DateRange> covers() const override { return range_; }

 private:
  std::optional<core::DateRange> range_;
};

std::vector<int> years_of(int from_year, int from_month, int from_day, int to_year, int to_month,
                          int to_day) {
  const Covering source(core::DateRange{core::Date{from_year, from_month, from_day},
                                        core::Date{to_year, to_month, to_day}});
  return whole_farm_years(source);
}

// **Ten calendar years hold ten farm years, not eleven.** The file the shipped
// Lincoln bundle uses runs 2015-01-01 to 2025-12-31: the first farm year it can
// run opens in July 2015, and the last closes in June 2025. Counting calendar
// years would have offered 2025-26, whose weather stops in December.
TEST(WholeYearsTest, TenCalendarYearsAreTenFarmYears) {
  const std::vector<int> years = years_of(2015, 1, 1, 2025, 12, 31);
  ASSERT_EQ(years.size(), 10U);
  EXPECT_EQ(years.front(), 2015);
  EXPECT_EQ(years.back(), 2024);
}

// The ends are inclusive: a file starting exactly on 1 July holds that year.
TEST(WholeYearsTest, AFileBeginningOnTheFirstOfJulyHoldsThatYear) {
  const std::vector<int> years = years_of(2015, 7, 1, 2017, 6, 30);
  ASSERT_EQ(years.size(), 2U);
  EXPECT_EQ(years.front(), 2015);
  EXPECT_EQ(years.back(), 2016);
}

// **A day short at either end drops a year**, which is the failure this exists
// to prevent: the run would otherwise ask for a day the file does not have.
TEST(WholeYearsTest, ADayShortAtEitherEndDropsThatYear) {
  EXPECT_EQ(years_of(2015, 7, 2, 2017, 6, 30).size(), 1U) << "opens a day late";
  EXPECT_EQ(years_of(2015, 7, 1, 2017, 6, 29).size(), 1U) << "closes a day early";
}

// Less than one whole farm year is none, not a partial one. A half-run year
// would compare against the others as a drought.
TEST(WholeYearsTest, HalfAYearIsNoYears) {
  EXPECT_TRUE(years_of(2015, 8, 1, 2016, 5, 31).empty());
}

// **A generator covers nothing and everything.** It will make any year asked of
// it, so it answers with no range - which has to mean "no particular years"
// rather than "no years", and the caller says there is nothing to compare
// instead of showing an empty table.
TEST(WholeYearsTest, AGeneratorHasNoParticularYears) {
  const Covering generator{std::nullopt};
  EXPECT_TRUE(whole_farm_years(generator).empty());
}

}  // namespace
}  // namespace paddock::app
