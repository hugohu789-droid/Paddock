#include <gtest/gtest.h>

#include <cstdint>
#include <stdexcept>

#include <paddock/core/SimulationClock.hpp>

namespace paddock::core {
namespace {

TEST(DateTest, EpochAndRoundTrip) {
  EXPECT_EQ((Date{1970, 1, 1}.days_since_epoch()), 0);
  EXPECT_EQ((Date{1969, 12, 31}.days_since_epoch()), -1);

  for (std::int64_t day = -3000; day <= 30000; day += 97) {
    const Date date = Date::from_days_since_epoch(day);
    EXPECT_TRUE(date.is_valid()) << date.to_iso_string();
    EXPECT_EQ(date.days_since_epoch(), day) << date.to_iso_string();
  }
}

TEST(DateTest, LeapYearsFollowTheGregorianRule) {
  EXPECT_TRUE(is_leap_year(2024));
  EXPECT_FALSE(is_leap_year(2023));
  EXPECT_FALSE(is_leap_year(1900));
  EXPECT_TRUE(is_leap_year(2000));
  EXPECT_EQ(days_in_month(2024, 2), 29);
  EXPECT_EQ(days_in_month(2023, 2), 28);
  EXPECT_EQ(days_in_month(2023, 13), 0);
}

TEST(DateTest, DayOfYearCountsFromTheFirstOfJanuary) {
  EXPECT_EQ((Date{2023, 1, 1}.day_of_year()), 1);
  EXPECT_EQ((Date{2023, 12, 31}.day_of_year()), 365);
  EXPECT_EQ((Date{2024, 12, 31}.day_of_year()), 366);
  EXPECT_EQ((Date{2024, 3, 1}.day_of_year()), 61);
}

TEST(DateTest, InvalidDatesAreRejected) {
  EXPECT_FALSE((Date{2023, 2, 29}.is_valid()));
  EXPECT_FALSE((Date{2023, 0, 1}.is_valid()));
  EXPECT_FALSE((Date{2023, 13, 1}.is_valid()));
  EXPECT_TRUE((Date{2024, 2, 29}.is_valid()));
}

TEST(DateTest, IsoFormattingPadsMonthAndDay) {
  EXPECT_EQ((Date{2024, 7, 1}.to_iso_string()), "2024-07-01");
  EXPECT_EQ((Date{2024, 11, 30}.to_iso_string()), "2024-11-30");
}

// Southern Hemisphere seasons: a farm calendar that put lambing in the northern
// spring would be wrong by six months.
TEST(SeasonTest, SeasonsAreSouthernHemisphere) {
  EXPECT_EQ(season_of(Date{2024, 1, 15}), Season::Summer);
  EXPECT_EQ(season_of(Date{2024, 12, 15}), Season::Summer);
  EXPECT_EQ(season_of(Date{2024, 4, 15}), Season::Autumn);
  EXPECT_EQ(season_of(Date{2024, 7, 15}), Season::Winter);
  EXPECT_EQ(season_of(Date{2024, 10, 15}), Season::Spring);
  EXPECT_EQ(season_name(Season::Winter), "winter");
}

// The NZ farm year runs 1 July to 30 June, and 365 steps from 1 July 2023 stop
// one day short of the next one because February 2024 has 29 days. A run length
// in days is not a run length in years: seasonal comparisons have to step to a
// date, not to a count.
TEST(SimulationClockTest, ThreeHundredAndSixtyFiveStepsFallShortOfALeapYear) {
  SimulationClock clock(Date{2023, 7, 1});

  EXPECT_EQ(clock.day_index(), 0);
  EXPECT_EQ(clock.date(), (Date{2023, 7, 1}));

  for (int day = 0; day < 365; ++day) {
    clock.advance();
  }

  EXPECT_EQ(clock.day_index(), 365);
  EXPECT_EQ(clock.date(), (Date{2024, 6, 30}));

  clock.advance();
  EXPECT_EQ(clock.date(), (Date{2024, 7, 1}));
  EXPECT_EQ(clock.start_date(), (Date{2023, 7, 1}));
}

TEST(SimulationClockTest, SteppingAYearFromANonLeapWindowLandsOnTheSameDate) {
  SimulationClock clock(Date{2022, 7, 1});

  clock.advance(365);

  EXPECT_EQ(clock.date(), (Date{2023, 7, 1}));
}

TEST(SimulationClockTest, AdvancingCrossesALeapDay) {
  SimulationClock clock(Date{2024, 2, 28});

  clock.advance(2);

  EXPECT_EQ(clock.date(), (Date{2024, 3, 1}));
}

TEST(SimulationClockTest, TimeDoesNotRunBackwards) {
  SimulationClock clock(Date{2024, 1, 1});

  EXPECT_THROW(clock.advance(-1), std::invalid_argument);
  EXPECT_THROW(SimulationClock(Date{2023, 2, 29}), std::invalid_argument);
}

TEST(SimulationClockTest, ResetReturnsToTheStartDate) {
  SimulationClock clock(Date{2024, 1, 1});
  clock.advance(200);

  clock.reset();

  EXPECT_EQ(clock.day_index(), 0);
  EXPECT_EQ(clock.date(), (Date{2024, 1, 1}));
}

}  // namespace
}  // namespace paddock::core
