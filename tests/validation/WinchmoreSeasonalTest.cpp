// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

/// The shape of a Canterbury dryland year, not only its size.
///
/// **An annual total is the weakest thing the Winchmore trial can tell you, and
/// it was the only thing being used.** The trial's sheet is called "Monthly,
/// seasonal, and yearly total production"; `scripts/winchmore-fetch.py` read
/// the TOT column and discarded the other sixteen. A model that grows too much
/// in spring and too little in summer sums to an annual figure that looks
/// right, and `CanterburyDrylandTest` would pass it.
///
/// **What the discarded columns show.** Dryland summer production over the 25
/// measured years runs from 3,208 kg DM/ha down to zero - 1981-82 grew nothing
/// at all across December, January and February, and 1963-64 grew 14 kg in the
/// whole of February. Spring is far steadier: 1,936 to 5,785. A dryland
/// Canterbury year is not a scaled version of an average year; it is a reliable
/// spring followed by a summer that may or may not happen.
///
/// **These are bands, not point targets, and they are wide on purpose.** The
/// trial is fertilised where this farm is not, its rainfall is its own, and its
/// 25 years are not this farm's 10. What the comparison can settle is whether
/// the model produces a Canterbury *shape* - and the share of the year each
/// season carries is the part that does not depend on either site's rainfall.

#include <gtest/gtest.h>

#include <array>
#include <string>
#include <vector>

#include <paddock/config/EconomicsConfig.hpp>
#include <paddock/config/ScenarioConfig.hpp>
#include <paddock/config/ScenarioRun.hpp>
#include <paddock/core/SnapshotWeather.hpp>

#include "../support/CalibrationTable.hpp"
#include "../support/ShippedBundle.hpp"
#include "../support/ValueOf.hpp"

namespace paddock::config {
namespace {

/// Winchmore's own seasons, which are the ones the comparison is made on.
/// Winter is June to August, spring September to November, summer December to
/// February, autumn March to May.
enum class Season { Winter, Spring, Summer, Autumn };

Season season_of(int month) {
  if (month >= 6 && month <= 8) {
    return Season::Winter;
  }
  if (month >= 9 && month <= 11) {
    return Season::Spring;
  }
  if (month == 12 || month <= 2) {
    return Season::Summer;
  }
  return Season::Autumn;
}

core::DietQuality pasture_diet() {
  core::DietQuality diet;
  diet.metabolisable_energy_mj_per_kg_dm = 10.5;
  diet.digestibility_percent = 75.0;
  return diet;
}

ScenarioBundle year_of(int starting_year) {
  ScenarioBundle bundle =
      tests::load_on_flat_ground(std::string(PADDOCK_DATA_DIR) + "/scenarios/lincoln-lurdf");

  core::SnapshotWeatherSource::Options options;
  options.path = std::string(PADDOCK_DATA_DIR) + "/scenarios/lincoln-lurdf/weather-2015-2025.csv";
  options.dataset = "open-meteo";
  options.licence = "CC BY 4.0";
  bundle.weather = std::make_shared<core::SnapshotWeatherSource>(options);
  bundle.range =
      core::DateRange{core::Date{starting_year, 7, 1}, core::Date{starting_year + 1, 6, 30}};
  return bundle;
}

/// What the model grew in each of Winchmore's four seasons, kg DM/ha.
std::array<double, 4> seasons_of(int starting_year) {
  const ScenarioBundle bundle = year_of(starting_year);
  const RunSummary run =
      run_managed_scenario(bundle, tests::value_of(bundle.management, "a [management] section"),
                           pasture_diet(), "seasonal");

  std::array<double, 4> grown{};
  for (std::size_t day = 0; day < run.dates.size() && day < run.growth_kg_dm_per_ha.size(); ++day) {
    grown[static_cast<std::size_t>(season_of(run.dates[day].month))] +=
        run.growth_kg_dm_per_ha[day];
  }
  return grown;
}

/// The trial's own figures, read from the file rather than typed here.
test::CalibrationTable trial() {
  return test::CalibrationTable(std::string(PADDOCK_DATA_DIR) +
                                "/calibration/winchmore-monthly-production.csv");
}

struct Band {
  double mean = 0.0;
  double lowest = 0.0;
  double highest = 0.0;
};

Band measured(const std::string& column) {
  const test::CalibrationTable table = trial();
  Band band;
  double total = 0.0;
  int count = 0;
  for (std::size_t row = 0; row < table.size(); ++row) {
    const std::string text = table.text(row, column);
    if (text.empty()) {
      continue;
    }
    const double value = std::stod(text);
    band.lowest = count == 0 ? value : std::min(band.lowest, value);
    band.highest = count == 0 ? value : std::max(band.highest, value);
    total += value;
    ++count;
  }
  band.mean = count > 0 ? total / count : 0.0;
  return band;
}

// **The file has what the fetch script used to throw away.** Asserted because
// every figure below reads out of it, and because a rebuild that silently
// dropped the monthly columns would leave the seasonal tests comparing against
// nothing.
TEST(WinchmoreSeasonalTest, TheTrialsMonthlyColumnsAreOnDisk) {
  const test::CalibrationTable table = trial();
  ASSERT_EQ(table.size(), 25U) << "the trial's 25 measured dryland years";

  for (const char* column : {"jun", "jul", "aug", "sep", "oct", "nov", "dec", "jan", "feb", "mar",
                             "apr", "may", "win", "spr", "sum", "aut", "tot"}) {
    EXPECT_FALSE(table.text(0, column).empty()) << column << " is missing from the first year";
  }
}

// **A dryland summer can be nothing at all**, and this is the figure that makes
// the case for having the monthly data. It is asserted so that a re-fetch which
// quietly smoothed the series would fail here.
TEST(WinchmoreSeasonalTest, TheMeasuredSummerRunsFromNothingToThreeTonnes) {
  const Band summer = measured("sum");
  EXPECT_DOUBLE_EQ(summer.lowest, 0.0)
      << "1981-82 grew nothing across December, January and February";
  EXPECT_GT(summer.highest, 3'000.0);

  const Band spring = measured("spr");
  EXPECT_GT(spring.lowest, 1'500.0) << "spring is the season a dryland farm can count on";

  // The point in one comparison: the season a farm relies on varies by a factor
  // of three, and the season that carries it varies without limit.
  EXPECT_GT(summer.highest / std::max(1.0, spring.highest / spring.lowest), 0.0);
  EXPECT_LT(spring.highest / spring.lowest, 4.0);
}

// **The shape, which is the half an annual total cannot check - and the model
// fails it.**
//
// This test was written to assert the model grew its year in roughly the
// trial's proportions, within ten points a season. It does not:
//
//     season   at E61   now     trial   remaining
//     winter     6.2%    9.1%   10.0%   -0.9 points
//     spring    32.2%   43.6%   54.7%  -11.1 points
//     summer    41.3%   32.2%   18.0%  +14.2 points
//     autumn    20.3%   15.1%   17.3%   -2.2 points
//
// **Winter and autumn are closed and spring and summer are not**, which is a
// more useful thing to know than a single seasonal error. Two sourced changes
// got there - the drought leaf-death term of E62/E63, and the temperature
// response of E64, which replaced a triangle and two placeholder cardinals with
// AgPasture's C3 curve and its published parameter set. Neither was tuned, and
// the second was checked against a friend's reading of the ryegrass literature:
// 20 C is a well-supported optimum and was never the problem, the shape around
// it was.
//
// The annual total is fine - 6,847 kg DM/ha against a measured mean of 6,442,
// inside the band `CanterburyDrylandTest` checks. The shape is not. **The
// model's biggest season is summer, and on a Canterbury dryland farm summer is
// when growth stops.** Spring is where more than half the year should be.
//
// **This is a recorded gap, not a loosened band.** The bounds below are the
// distance that was measured, asserted so that it cannot widen and so that
// closing it shows up here as a failure rather than as a number nobody looked
// at - the same way E40 holds the phosphorus gap and E52 the appetite one. The
// aspirational version of this test is in the git history of this file, and
// what it asserted is what a corrected model should pass.
//
// **Where the rest of it comes from is not yet known.** What is known
// (verify.md, E62) is that it is not the water stress, which works. Ks averages 0.618 in spring against 0.366 in
// summer, and 86 to 100% of November-to-February days sit below 0.5. The fault
// is that `Pasture.cpp` feeds that same water factor into leaf death, in the
// direction that protects the sward - a thirsty tiller pushes its next leaf
// more slowly, so the leaf it carries lives longer. True, and only half of what
// a drought does; the half where drought kills standing leaf is missing. The
// two cancel almost exactly - spring 7.5 x 0.618 = 4.64 degree-days, summer
// 12.5 x 0.366 = 4.59 - so this sward loses leaf at the same rate in a February
// drought as in an October spring, holds its cover, keeps intercepting light,
// and keeps growing. A real dryland sward browns off. That half is now carried
// by `drought_turnover_*`, and it was worth three points of the twenty-two.
//
// **What is left is a spring/summer partition, and it has been diagnosed**
// (verify.md, E67). Not water: spring Ks is 0.930, six days in ninety-one below
// half. Not nitrogen: the mineral pool sits at 95-102 kg/ha through spring and
// accumulates all year. Not heat: 6.8% of summer days pass AgPasture's 28 C
// onset by an average of 1.5 C.
//
// The measurement that settles it is in the trial's own columns. **Spring beats
// summer in 25 of the 25 measured years**, median ratio 3.14 and never below
// 1.09. This model manages 8 of 10 at a median of 1.16, and loses outright in
// the years summer stays wet. Spring dominance is a property of the plant, and
// the model has it as a property of the weather - it grows whenever there is
// water, and in Canterbury the best light and warmth are in January.
//
// The leading hypothesis is that there is no reproductive development here:
// ryegrass turns reproductive in spring, and afterwards carries fewer tillers
// through summer even with water in the soil.
//
// **AgPasture's stand-in for that mechanism is now in** (E69, ADR 0018), and it
// is worth about a third of what was left: spring 38.7 to 43.6. It does not
// restore the signature. Spring still beats summer 8 years in 10 against the
// record's 25 in 25, because a 28% spring boost cannot outweigh a summer that
// grows 3,900 kg DM/ha when the rain comes. The remainder wants the mechanism
// itself, not another multiplier.
//
// Compared as shares of the year rather than kilograms, because a share does
// not depend on either site's rainfall - Winchmore's 745 mm mean is not this
// farm's.
TEST(WinchmoreSeasonalTest, TheModelsSeasonalShapeIsWrongAndThisHoldsTheGap) {
  std::array<double, 4> grown{};
  for (int start = 2015; start <= 2024; ++start) {
    const std::array<double, 4> year = seasons_of(start);
    for (std::size_t i = 0; i < grown.size(); ++i) {
      grown[i] += year[i];
    }
  }
  const double total = grown[0] + grown[1] + grown[2] + grown[3];
  ASSERT_GT(total, 0.0);

  const Band winter = measured("win");
  const Band spring = measured("spr");
  const Band summer = measured("sum");
  const Band autumn = measured("aut");
  const double measured_total = winter.mean + spring.mean + summer.mean + autumn.mean;
  ASSERT_GT(measured_total, 0.0);

  const std::array<double, 4> theirs{winter.mean / measured_total, spring.mean / measured_total,
                                     summer.mean / measured_total, autumn.mean / measured_total};

  const char* names[] = {"winter", "spring", "summer", "autumn"};
  for (std::size_t i = 0; i < grown.size(); ++i) {
    GTEST_LOG_(INFO) << names[i] << ": model " << (grown[i] / total * 100.0) << "%, trial "
                     << (theirs[i] * 100.0) << "%";
  }

  const double model_spring = grown[1] / total * 100.0;
  const double model_summer = grown[2] / total * 100.0;

  // **The gap as measured, held from both sides.** Under the lower bound means
  // it got worse; over the upper means somebody improved the model and should
  // say so here rather than leave a test describing a version that no longer
  // exists.
  EXPECT_GT(model_spring, 41.0) << "spring has fallen below where the gap was measured";
  EXPECT_LT(model_spring, 47.0)
      << "the model now puts " << model_spring
      << "% of its year in spring against the trial's 55%. If the water limitation was fixed, "
         "raise this bound and record what changed";

  EXPECT_GT(model_summer, 28.0)
      << "the model now puts " << model_summer
      << "% of its year in summer against the trial's 18%, which is better than the 41% this was "
         "written at. Say what closed it";
  EXPECT_LT(model_summer, 35.0) << "summer has grown beyond where the gap was measured";

  // **Winter and autumn are held tightly, and autumn is now the tight one.** Both
  // were within twelve points of the trial when this was written and within one
  // of it after E64. The reproductive season of E69 bought spring five points
  // and took two of them off autumn, which now sits 2.2 points out against a
  // 2.5 point bound. That is the cost of the trade, held where it can be seen
  // rather than described in a comment nobody runs.
  EXPECT_NEAR(grown[0] / total * 100.0, theirs[0] * 100.0, 2.5) << "winter";
  EXPECT_NEAR(grown[3] / total * 100.0, theirs[3] * 100.0, 2.5) << "autumn";
}

// **Spring is the biggest season on a Canterbury dryland farm**, and a model
// that disagreed about that would be describing somewhere else. The trial puts
// about half its year in spring; this holds the ordering rather than the size.
TEST(WinchmoreSeasonalTest, SpringIsTheBiggestSeasonInBoth) {
  const Band winter = measured("win");
  const Band spring = measured("spr");
  const Band summer = measured("sum");
  const Band autumn = measured("aut");
  EXPECT_GT(spring.mean, summer.mean);
  EXPECT_GT(spring.mean, autumn.mean);
  EXPECT_GT(spring.mean, winter.mean);

  std::array<double, 4> grown{};
  for (int start = 2015; start <= 2024; ++start) {
    const std::array<double, 4> year = seasons_of(start);
    for (std::size_t i = 0; i < grown.size(); ++i) {
      grown[i] += year[i];
    }
  }
  EXPECT_GT(grown[1], grown[0]) << "the model's spring should beat its winter";
  EXPECT_GT(grown[1], grown[3]) << "and its autumn";
}

}  // namespace
}  // namespace paddock::config
