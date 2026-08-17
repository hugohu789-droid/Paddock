// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

// T3: the model against measured data.
//
// The example scenario is run for twenty farm years and its mean monthly growth
// rates are compared with DairyNZ's published averages for two South Island
// sites. What
// is asserted is the *shape* of the season - when growth peaks, when it stops,
// how the year's total is distributed across the months - because that is what
// the model can honestly be held to today:
//
//   * Paddock's only nitrogen income is clover fixation, about 46 kg N/ha,
//     while every Canterbury site on DairyNZ's sheet received 154 to 330 kg
//     N/ha of fertiliser. Matching those sites in magnitude would mean the
//     model was wrong.
//   * The growth parameters are placeholders (docs/verify.md, E7). Calibrating
//     the magnitude is what this gate is for; today it measures the gap and
//     writes it down rather than pretending it is closed.
//
// The comparison series is written to a CSV that CI turns into a plot and keeps
// as an artifact, so the gap is visible on every pull request rather than
// discovered at the end of a milestone.

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <fstream>
#include <string>

#include <paddock/config/ScenarioConfig.hpp>
#include <paddock/core/Simulation.hpp>

#include "support/CalibrationSeries.hpp"

namespace paddock {
namespace {

using test_support::CalibrationSeries;
using test_support::correlation;
using test_support::load_calibration_series;

constexpr std::array<const char*, 12> kMonthNames = {"jan", "feb", "mar", "apr", "may", "jun",
                                                     "jul", "aug", "sep", "oct", "nov", "dec"};

/// The first farm year of the run is spin-up and is compared with nothing.
constexpr int kSpinUpEndsYear = 2001;

std::string data_path(const std::string& relative) {
  return std::string(PADDOCK_DATA_DIR) + "/" + relative;
}

/// Mean daily growth for each calendar month of a run, kg DM/ha/day.
std::array<double, 12> monthly_growth(const core::RunResult& result) {
  std::array<double, 12> totals{};
  std::array<int, 12> days{};
  for (const core::DailyRecord& record : result.daily) {
    if (record.date.year < kSpinUpEndsYear ||
        (record.date.year == kSpinUpEndsYear && record.date.month < 7)) {
      continue;  // spin-up
    }
    const auto month = static_cast<std::size_t>(record.date.month - 1);
    totals[month] += record.growth_kg_dm;
    ++days[month];
  }
  std::array<double, 12> means{};
  for (std::size_t month = 0; month < means.size(); ++month) {
    means[month] = days[month] > 0 ? totals[month] / days[month] : 0.0;
  }
  return means;
}

std::array<double, 12> shares_of(const std::array<double, 12>& monthly) {
  double total = 0.0;
  for (const double value : monthly) {
    total += value;
  }
  std::array<double, 12> shares{};
  if (total <= 0.0) {
    return shares;
  }
  for (std::size_t month = 0; month < shares.size(); ++month) {
    shares[month] = monthly[month] / total;
  }
  return shares;
}

std::size_t index_of_maximum(const std::array<double, 12>& monthly) {
  std::size_t best = 0;
  for (std::size_t month = 1; month < monthly.size(); ++month) {
    if (monthly[month] > monthly[best]) {
      best = month;
    }
  }
  return best;
}

std::size_t index_of_minimum(const std::array<double, 12>& monthly) {
  std::size_t worst = 0;
  for (std::size_t month = 1; month < monthly.size(); ++month) {
    if (monthly[month] < monthly[worst]) {
      worst = month;
    }
  }
  return worst;
}

/// Twenty farm years of the example scenario, the first discarded.
///
/// Two things this arrangement exists for, both learned the hard way:
///
///   * **A spin-up year.** The initial soil water and pasture cover are a guess,
///     and a run that starts in January spends its first month growing on
///     whatever water it was handed. Discarding the first year removes that.
///     Starting the year on 1 July helps too, which is one reason the New
///     Zealand farm year does.
///   * **Twenty years, not one.** The measured series are multi-year averages
///     (2007-2012, 2012-2015). Comparing a single simulated year against them
///     compares weather noise with a mean, and one dry February is enough to
///     fail a gate that should be measuring the model.
const core::RunResult& modelled_run() {
  static const core::RunResult result = [] {
    const config::ScenarioBundle bundle =
        config::load_scenario(data_path("scenarios/canterbury-baseline"));
    static core::Farmlet farmlet = bundle.make_farmlet();
    const core::DateRange twenty_years{core::Date{2000, 7, 1}, core::Date{2020, 6, 30}};
    return core::run(farmlet, *bundle.weather, twenty_years);
  }();
  return result;
}

CalibrationSeries reference(const std::string& site) {
  return load_calibration_series(data_path("calibration/dairynz-pasture-growth.csv"), site);
}

// The comparison a reviewer looks at. Written on every run so the plot in CI is
// never stale relative to the assertions beside it.
TEST(SeasonalGrowthValidation, WritesTheComparisonSeries) {
  const std::array<double, 12> modelled = monthly_growth(modelled_run());
  const CalibrationSeries lincoln = reference("lincoln_p21_low_n");
  const CalibrationSeries woodlands = reference("woodlands_zero_n");

  const std::string path = std::string(PADDOCK_VALIDATION_OUTPUT_DIR) + "/seasonal-growth.csv";
  std::ofstream out(path);
  ASSERT_TRUE(out.good()) << "cannot write " << path;
  out << "month,modelled_kg_dm_per_ha_per_day,lincoln_p21_low_n,woodlands_zero_n\n";
  for (std::size_t month = 0; month < 12; ++month) {
    out << kMonthNames.at(month) << ',' << modelled.at(month) << ','
        << lincoln.monthly_kg_dm_per_ha_per_day.at(month) << ','
        << woodlands.monthly_kg_dm_per_ha_per_day.at(month) << '\n';
  }
  out.close();
  GTEST_LOG_(INFO) << "wrote " << path;
}

// The shape of a Southern Hemisphere pasture year: growth peaks in late spring
// or early summer and all but stops in midwinter.
TEST(SeasonalGrowthValidation, ThePeakAndTroughFallInTheRightSeasons) {
  const std::array<double, 12> modelled = monthly_growth(modelled_run());

  const std::size_t peak = index_of_maximum(modelled);
  const std::size_t trough = index_of_minimum(modelled);

  EXPECT_TRUE(peak >= 9 || peak <= 1)
      << "peak growth in " << kMonthNames.at(peak) << ", expected October to February";
  EXPECT_TRUE(trough >= 5 && trough <= 7)
      << "lowest growth in " << kMonthNames.at(trough) << ", expected June to August";

  // Both measured sites peak in the same window, which is what makes this a
  // comparison rather than an assertion about our own output.
  const std::size_t lincoln_peak =
      index_of_maximum(reference("lincoln_p21_low_n").monthly_kg_dm_per_ha_per_day);
  EXPECT_TRUE(lincoln_peak >= 9 || lincoln_peak <= 1);
}

// The tolerance band, against the unfertilised site.
//
// The band is on each month's *share* of the year's growth: four percentage
// points, wide enough for an uncalibrated model and narrow enough that a
// wrongly shaped season fails - one that grew evenly all year would miss by
// fifteen.
//
// It is measured against Woodlands, the only site on DairyNZ's sheet with no
// nitrogen fertiliser, and this test failed against Lincoln before it was.
// Lincoln's November share is 11.2% against this model's 15.9%; Woodlands' is
// 16.6%. Nitrogen fertiliser flattens the seasonal curve - it lifts the
// shoulders of the season, when temperature and slow mineralisation would
// otherwise hold growth back - so a fertilised site spreads its growth more
// evenly than a clover-based one, which concentrates it in the months clover
// fixes in. Holding an unfertilised model to a fertilised distribution asks it
// to reproduce fertiliser it never received.
TEST(SeasonalGrowthValidation, TheSeasonalDistributionIsWithinTolerance) {
  const std::array<double, 12> modelled = shares_of(monthly_growth(modelled_run()));
  const CalibrationSeries woodlands = reference("woodlands_zero_n");
  const std::array<double, 12> measured = woodlands.monthly_shares();

  constexpr double kToleranceShare = 0.04;
  for (std::size_t month = 0; month < 12; ++month) {
    EXPECT_NEAR(modelled.at(month), measured.at(month), kToleranceShare)
        << kMonthNames.at(month) << ": modelled " << modelled.at(month) * 100.0
        << "% of the year, measured " << measured.at(month) * 100.0 << "%";
  }
}

TEST(SeasonalGrowthValidation, TheSeasonalCurvesCorrelate) {
  const std::array<double, 12> modelled = monthly_growth(modelled_run());

  const double lincoln =
      correlation(modelled, reference("lincoln_p21_low_n").monthly_kg_dm_per_ha_per_day);
  const double woodlands =
      correlation(modelled, reference("woodlands_zero_n").monthly_kg_dm_per_ha_per_day);

  GTEST_LOG_(INFO) << "correlation with lincoln_p21_low_n: " << lincoln
                   << ", with woodlands_zero_n: " << woodlands;
  // The unfertilised site is the closer match, which is the result this gate
  // was built to be able to state.
  EXPECT_GT(woodlands, 0.90);
  EXPECT_GT(lincoln, 0.85);
  EXPECT_GT(woodlands, lincoln);
}

// Magnitude is not asserted tightly, and this test says why in the failure
// message rather than in a comment nobody reads.
TEST(SeasonalGrowthValidation, TheMagnitudeGapIsRecorded) {
  const core::RunResult& result = modelled_run();
  const CalibrationSeries lincoln = reference("lincoln_p21_low_n");
  const CalibrationSeries woodlands = reference("woodlands_zero_n");

  // Per year, over the compared period rather than over the whole run.
  std::array<double, 12> monthly = monthly_growth(result);
  double modelled_t = 0.0;
  constexpr std::array<int, 12> kDaysInMonth = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  for (std::size_t month = 0; month < monthly.size(); ++month) {
    modelled_t += monthly.at(month) * kDaysInMonth.at(month);
  }
  modelled_t /= 1000.0;

  GTEST_LOG_(INFO) << "annual growth: modelled " << modelled_t << " t DM/ha, lincoln_p21_low_n "
                   << lincoln.annual_t_dm_per_ha << " t DM/ha ("
                   << lincoln.nitrogen_applied_kg_per_ha << " kg N/ha applied), woodlands_zero_n "
                   << woodlands.annual_t_dm_per_ha
                   << " t DM/ha (unfertilised); modelled nitrogen income "
                   << result.summary.total_nitrogen_fixed_kg /
                          (static_cast<double>(result.summary.days) / 365.0)
                   << " kg N/ha/yr, all of it fixation";

  // An unfertilised model should land below a fertilised site and in the
  // neighbourhood of the unfertilised one. Anything outside this is not a
  // calibration problem but a broken model.
  EXPECT_LT(modelled_t, lincoln.annual_t_dm_per_ha)
      << "the model grew more than a site given 154 kg N/ha of fertiliser it does not have";
  EXPECT_GT(modelled_t, woodlands.annual_t_dm_per_ha * 0.5);
  EXPECT_LT(modelled_t, woodlands.annual_t_dm_per_ha * 2.0);
}

}  // namespace
}  // namespace paddock
