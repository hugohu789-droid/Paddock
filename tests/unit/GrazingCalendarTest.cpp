// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

// Which grazing system a farm is running, and when.
//
// The property that matters is the same one PaddockMask has, one axis over:
// every simulated day is under exactly one rule. A gap is a day nobody manages
// and an overlap is two managements at once, and both would surface downstream
// as pasture behaving oddly rather than as a plan that does not add up.
//
// The default calendar's shape comes from Smith and Dawson (1976). Where an
// assertion below checks a number, it is theirs; where it checks a structure,
// it is the partition property.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include <paddock/core/GrazingCalendar.hpp>

namespace paddock::core {
namespace {

/// A southern North Island breeding property: lambing in late August, tupping
/// in mid-March, lambs weaned at ten weeks.
FarmYearEvents hill_country_events() {
  FarmYearEvents events;
  events.lambing_start = Date{2023, 8, 20};
  events.tupping_start = Date{2023, 3, 15};
  events.weaning_age_days = 70;
  events.pre_lambing_shortening_days = 21;
  return events;
}

/// The New Zealand farm year, 1 July to 30 June.
DateRange farm_year(int starting_year) {
  return DateRange{Date{starting_year, 7, 1}, Date{starting_year + 1, 6, 30}};
}

std::vector<Date> every_day(const DateRange& range) {
  std::vector<Date> days;
  days.reserve(static_cast<std::size_t>(range.day_count()));
  for (std::int64_t offset = 0; offset < range.day_count(); ++offset) {
    days.push_back(Date::from_days_since_epoch(range.first.days_since_epoch() + offset));
  }
  return days;
}

// The whole point: no day of the run is left without a rule, and no day has two.
TEST(GrazingCalendarTest, TheDefaultCoversEveryDayOfTheRunExactlyOnce) {
  const DateRange run = farm_year(2023);
  const GrazingCalendar calendar = default_grazing_calendar(hill_country_events(), run);

  ASSERT_TRUE(calendar.validation_error(run).empty()) << calendar.validation_error(run);

  for (const Date& day : every_day(run)) {
    int covering = 0;
    for (const GrazingPeriod& period : calendar.periods()) {
      if (period.dates.contains(day)) {
        ++covering;
      }
    }
    ASSERT_EQ(covering, 1) << day.to_iso_string() << " is under " << covering << " rules";
  }
}

// A run of any length, and one that does not begin on a farm-year boundary.
TEST(GrazingCalendarTest, ItCoversMultiYearAndOffsetRuns) {
  const FarmYearEvents events = hill_country_events();

  for (const DateRange run : {DateRange{Date{2024, 1, 15}, Date{2024, 3, 2}},
                              DateRange{Date{2023, 7, 1}, Date{2028, 6, 30}},
                              DateRange{Date{2030, 2, 1}, Date{2031, 11, 30}}}) {
    const GrazingCalendar calendar = default_grazing_calendar(events, run);
    EXPECT_TRUE(calendar.validation_error(run).empty())
        << run.first.to_iso_string() << " to " << run.last.to_iso_string() << ": "
        << calendar.validation_error(run);
    // Every day resolves without throwing.
    for (const Date& day : every_day(run)) {
      ASSERT_NO_THROW(static_cast<void>(calendar.rule_on(day))) << day.to_iso_string();
    }
  }
}

// The run starting years away from the event dates is the case that breaks a
// calendar anchored to the events' own year rather than to the run's.
TEST(GrazingCalendarTest, TheEventYearDoesNotHaveToBeTheRunYear) {
  FarmYearEvents events = hill_country_events();
  events.lambing_start = Date{1999, 8, 20};
  events.tupping_start = Date{1999, 3, 15};

  const DateRange run = farm_year(2035);
  const GrazingCalendar calendar = default_grazing_calendar(events, run);

  EXPECT_TRUE(calendar.validation_error(run).empty()) << calendar.validation_error(run);
}

// Smith and Dawson: "The stock are set-stocked as close to lambing as is
// practical", and lambs "weaned by an average age of ten weeks", at which point
// the ewes "begin on the rotation of the whole farm".
TEST(GrazingCalendarTest, StockAreSetStockedFromLambingUntilWeaning) {
  const FarmYearEvents events = hill_country_events();
  const DateRange run = farm_year(2023);
  const GrazingCalendar calendar = default_grazing_calendar(events, run);

  EXPECT_EQ(calendar.rule_on(Date{2023, 8, 20}).system, GrazingSystem::SetStocking)
      << "lambing day";
  EXPECT_EQ(calendar.rule_on(Date{2023, 9, 15}).system, GrazingSystem::SetStocking)
      << "mid-lambing";

  // Ten weeks after 20 August is 29 October.
  EXPECT_EQ(events.weaning_date(), (Date{2023, 10, 29}));
  EXPECT_EQ(calendar.rule_on(Date{2023, 10, 28}).system, GrazingSystem::SetStocking)
      << "the day before weaning";
  EXPECT_EQ(calendar.rule_on(Date{2023, 10, 29}).system, GrazingSystem::Rotational)
      << "rotation begins at weaning";
}

// "As the drier summer approaches, the length of rotation is increased from an
// initial 21 days to 35 days. This spelling of pastures before the end of
// November is most beneficial in increasing clover content."
TEST(GrazingCalendarTest, TheRotationLengthensFromTwentyOneDaysToThirtyFive) {
  const GrazingCalendar calendar = default_grazing_calendar(hill_country_events(), farm_year(2023));

  EXPECT_EQ(calendar.rule_on(Date{2023, 11, 15}).minimum_spell_days, 21);
  EXPECT_EQ(calendar.rule_on(Date{2023, 12, 15}).minimum_spell_days, 35);
  EXPECT_EQ(calendar.rule_on(Date{2024, 2, 1}).minimum_spell_days, 35);

  // "Do not graze a pasture for more than three days with the major grazing
  // mob" applies wherever the farm is rotating.
  EXPECT_EQ(calendar.rule_on(Date{2023, 12, 15}).maximum_graze_days, 3);
}

// "The length of rotation is increased from tupping to three weeks before the
// start of lambing", then "three weeks before lambing the length of rotation is
// gradually reduced".
//
// The run has to reach past 30 July 2024 for this to test anything: a farm year
// ending on 30 June stops before the shortening begins, and the assertions
// would pass without ever seeing it.
TEST(GrazingCalendarTest, TheRotationShortensInTheThreeWeeksBeforeLambing) {
  const DateRange run{Date{2023, 7, 1}, Date{2024, 9, 30}};
  const GrazingCalendar calendar = default_grazing_calendar(hill_country_events(), run);
  ASSERT_TRUE(calendar.validation_error(run).empty()) << calendar.validation_error(run);

  // Lambing is 20 August 2024; three weeks before it is 30 July.
  EXPECT_EQ(calendar.rule_on(Date{2024, 7, 29}).minimum_spell_days, 35)
      << "the day before the shortening starts";
  EXPECT_EQ(calendar.rule_on(Date{2024, 7, 30}).minimum_spell_days, 21) << "shortening begins";
  EXPECT_EQ(calendar.rule_on(Date{2024, 8, 19}).minimum_spell_days, 21)
      << "the last day before lambing";

  // And then the farm sets stock for lambing again, closing the annual cycle.
  EXPECT_EQ(calendar.rule_on(Date{2024, 8, 20}).system, GrazingSystem::SetStocking);
  EXPECT_EQ(calendar.rule_on(Date{2024, 7, 30}).system, GrazingSystem::Rotational);
}

// Moving lambing moves the whole plan, because the source states it relative to
// the farm's own events rather than to calendar dates.
TEST(GrazingCalendarTest, ChangingTheLambingDateShiftsTheWholeCalendar) {
  const DateRange run = farm_year(2023);

  const FarmYearEvents early = hill_country_events();
  FarmYearEvents late = hill_country_events();
  late.lambing_start = Date{2023, 9, 20};  // a month later

  const GrazingCalendar early_plan = default_grazing_calendar(early, run);
  const GrazingCalendar late_plan = default_grazing_calendar(late, run);

  // On 15 September the early-lambing farm is set stocked and the late one is
  // still rotating, because its ewes have not lambed yet.
  EXPECT_EQ(early_plan.rule_on(Date{2023, 9, 15}).system, GrazingSystem::SetStocking);
  EXPECT_EQ(late_plan.rule_on(Date{2023, 9, 15}).system, GrazingSystem::Rotational);

  EXPECT_TRUE(late_plan.validation_error(run).empty()) << late_plan.validation_error(run);
}

// A system may appear as often as the plan needs it, with different parameters
// each time. The source's own calendar rotates at three different points in the
// year, so a calendar allowing one entry per system could not express it.
TEST(GrazingCalendarTest, TheSameSystemAppearsMoreThanOnceWithDifferentParameters) {
  const GrazingCalendar calendar = default_grazing_calendar(hill_country_events(), farm_year(2023));

  int rotational_periods = 0;
  std::set<int> spells;
  for (const GrazingPeriod& period : calendar.periods()) {
    if (period.rule.system == GrazingSystem::Rotational) {
      ++rotational_periods;
      spells.insert(period.rule.minimum_spell_days);
    }
  }

  EXPECT_GE(rotational_periods, 2) << "the source's calendar rotates at several points";
  EXPECT_GE(spells.size(), 2U) << "and at more than one spell length";
}

// A hand-written plan is the point of the feature, so the validator has to
// catch the two ways it can be wrong.
TEST(GrazingCalendarTest, AGapInAHandWrittenPlanIsRejectedAndNamed) {
  const DateRange run{Date{2024, 1, 1}, Date{2024, 3, 31}};

  const GrazingCalendar calendar(std::vector<GrazingPeriod>{
      GrazingPeriod{"first", DateRange{Date{2024, 1, 1}, Date{2024, 1, 31}}, GrazingRule{}},
      // February is missing.
      GrazingPeriod{"second", DateRange{Date{2024, 3, 1}, Date{2024, 3, 31}}, GrazingRule{}}});

  const std::string error = calendar.validation_error(run);
  EXPECT_FALSE(error.empty());
  EXPECT_NE(error.find("2024-02-01"), std::string::npos) << error;
}

TEST(GrazingCalendarTest, AnOverlapInAHandWrittenPlanIsRejectedAndNamed) {
  const DateRange run{Date{2024, 1, 1}, Date{2024, 3, 31}};

  const GrazingCalendar calendar(std::vector<GrazingPeriod>{
      GrazingPeriod{"first", DateRange{Date{2024, 1, 1}, Date{2024, 2, 15}}, GrazingRule{}},
      GrazingPeriod{"second", DateRange{Date{2024, 2, 10}, Date{2024, 3, 31}}, GrazingRule{}}});

  const std::string error = calendar.validation_error(run);
  EXPECT_FALSE(error.empty());
  EXPECT_NE(error.find("2024-02-10"), std::string::npos) << error;
}

TEST(GrazingCalendarTest, APlanThatStopsBeforeTheRunDoesIsRejected) {
  const DateRange run{Date{2024, 1, 1}, Date{2024, 12, 31}};

  const GrazingCalendar calendar(std::vector<GrazingPeriod>{
      GrazingPeriod{"only", DateRange{Date{2024, 1, 1}, Date{2024, 6, 30}}, GrazingRule{}}});

  EXPECT_FALSE(calendar.validation_error(run).empty());
  EXPECT_THROW(static_cast<void>(calendar.rule_on(Date{2024, 8, 1})), std::out_of_range);
}

// A rotation that spells for no longer than it grazes is set stocking wearing a
// different name, and letting it through would make a comparison of the two
// systems meaningless.
TEST(GrazingCalendarTest, ARotationThatDoesNotActuallySpellIsRejected) {
  GrazingRule pretend_rotation;
  pretend_rotation.system = GrazingSystem::Rotational;
  pretend_rotation.maximum_graze_days = 5;
  pretend_rotation.minimum_spell_days = 3;

  EXPECT_FALSE(pretend_rotation.validation_error().empty());
  EXPECT_THROW(GrazingCalendar(std::vector<GrazingPeriod>{GrazingPeriod{
                   "pretend", DateRange{Date{2024, 1, 1}, Date{2024, 1, 31}}, pretend_rotation}}),
               std::invalid_argument);
}

TEST(GrazingCalendarTest, ImpossibleFarmEventsAreRefused) {
  FarmYearEvents events = hill_country_events();
  events.weaning_age_days = 0;
  EXPECT_THROW(static_cast<void>(default_grazing_calendar(events, farm_year(2023))),
               std::invalid_argument);

  FarmYearEvents bad_date = hill_country_events();
  bad_date.lambing_start = Date{2023, 13, 1};
  EXPECT_THROW(static_cast<void>(default_grazing_calendar(bad_date, farm_year(2023))),
               std::invalid_argument);
}

}  // namespace
}  // namespace paddock::core
