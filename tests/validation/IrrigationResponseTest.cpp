// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

// What irrigation does to annual production, against what it measurably did.
//
// **The only external check this project has on irrigation.** Every other
// validation here asks what a rain-fed Canterbury sward produces; this asks
// what happens when you water it, which is the question the flagship
// demonstration is built to answer and the one nothing had checked. Winchmore
// ran three irrigated treatments beside a dryland control for twenty-five farm
// years and the columns sat in `winchmore-annual-production.csv` unread until
// E88 found them.
//
// **What is gated and what is only reported.** The annual response ratio is
// gated, against a band taken from the measurement. The percentile the model
// lands at is reported, because fifty treatment-years from one trial on one
// soil is a sample and a pass condition written on a rank would claim more of
// that sample than it can carry. The seasonal split is not gated at all - it is
// a known open error (E84) and a gate would either fail on the day it was
// written or be set so wide it asserted nothing - but it carries a regression
// guard, which is a different thing and is labelled as one.
//
// Nothing here tunes anything. If the model stops passing, the model changed.

#include <gtest/gtest.h>

#include <cstddef>
#include <iostream>
#include <string>
#include <vector>

#include <paddock/config/DemoSummary.hpp>
#include <paddock/config/EconomicsConfig.hpp>
#include <paddock/config/FarmDashboard.hpp>
#include <paddock/config/ScenarioRun.hpp>

#include "../support/ShippedBundle.hpp"
#include "../support/ValueOf.hpp"
#include "../support/WinchmoreResponse.hpp"

namespace paddock::config {
namespace {

std::string data_dir() {
  return {PADDOCK_DATA_DIR};
}

core::DietQuality pasture_diet() {
  core::DietQuality diet;
  diet.metabolisable_energy_mj_per_kg_dm = 10.5;
  diet.digestibility_percent = 75.0;
  return diet;
}

/// One half of the flagship pair, run and read as the demonstration reads it.
///
/// **Through `build_dashboard`, not off the ledger directly.** The gate has to
/// be on the number the customer is shown; taking the same quantity by another
/// route is how a test comes to pass for a figure nobody sees.
struct Half {
  RunSummary run;
  FarmDashboard board;

  [[nodiscard]] double grown_kg_dm_per_ha() const {
    for (const Indicator& indicator : board.all_indicators()) {
      if (indicator.name == "Pasture grown") {
        return indicator.value;
      }
    }
    ADD_FAILURE() << "the dashboard has no 'Pasture grown' indicator";
    return 0.0;
  }
};

Half run_half(const std::string& which) {
  const ScenarioBundle bundle =
      tests::load_on_flat_ground(data_dir() + "/scenarios/demo-irrigation-" + which);

  Half half;
  // **With the bundle's own economics, because that is the farm being shown.**
  // Without them the flock never advances, and E79 measured what that costs:
  // the same Lincoln bundle reports 940 kg DM/ha eaten at 13% utilisation
  // where its own economics give 1,908 at 66%. A gate run on the unpriced path
  // would be gating a different farm from the one on the demonstration page -
  // it read 6,763 kg DM/ha against the 6,407 the customer sees, and a ratio
  // taken from that is arithmetic about nothing.
  if (!bundle.economics_path.empty()) {
    const FarmEconomics economics = load_economics(bundle.economics_path);
    half.run = run_managed_scenario(bundle, tests::value_of(bundle.management, "[management]"),
                                    pasture_diet(), which, business_from(bundle, economics));
  } else {
    half.run = run_managed_scenario(bundle, pasture_diet(), which);
  }
  half.board = build_dashboard(bundle, half.run, which);
  return half;
}

/// Growth in December, January and February as a share of the year.
double summer_share(const RunSummary& run) {
  double summer = 0.0;
  double year = 0.0;
  for (std::size_t day = 0; day < run.dates.size() && day < run.growth_kg_dm_per_ha.size(); ++day) {
    const int month = run.dates[day].month;
    year += run.growth_kg_dm_per_ha[day];
    if (month == 12 || month == 1 || month == 2) {
      summer += run.growth_kg_dm_per_ha[day];
    }
  }
  return year > 0.0 ? summer / year : 0.0;
}

// ---------------------------------------------------------------------------
// The band, against the file it came from.

// **The constants are checked against their own source.** `kIrrigationResponse*`
// are written down in a header so the demo page can carry them without reading
// a CSV, and a number written down is a number that can drift from what it was
// derived from. This is the one test that would notice.
TEST(IrrigationResponseTest, TheAcceptanceBandStillBracketsTheMeasurement) {
  const tests::WinchmoreResponse measured = tests::winchmore_wet_treatments(data_dir());

  ASSERT_EQ(measured.treatment_years(), 50U)
      << "the wet treatments should give 25 years x 2 treatments";

  const double p10 = measured.percentile(10.0);
  const double p90 = measured.percentile(90.0);

  // **The band sits inside the percentiles it was rounded from**, which is the
  // strict direction: a gate a fraction tighter than its sample can raise a
  // false alarm and can never wave a bad model through.
  EXPECT_GE(kIrrigationResponseLow, p10)
      << "the low bound has been widened past the measured 10th percentile of " << p10;
  EXPECT_LE(kIrrigationResponseHigh, p90)
      << "the high bound has been widened past the measured 90th percentile of " << p90;

  // And not so far inside that it has stopped describing the measurement. Half
  // a percentile either way is rounding; more than that is a different band
  // wearing this one's provenance.
  EXPECT_LT(kIrrigationResponseLow - p10, 0.05)
      << "the low bound is no longer the 10th percentile rounded, it is a choice";
  EXPECT_LT(p90 - kIrrigationResponseHigh, 0.10)
      << "the high bound is no longer the 90th percentile rounded, it is a choice";
}

// ---------------------------------------------------------------------------
// The gate.

// **The flagship comparison's annual response, against twenty-five measured
// years.** This is the pass condition; everything else this file prints is
// context for reading it.
TEST(IrrigationResponseTest, TheFlagshipResponseIsInsideTheMeasuredRange) {
  const Half rain_fed = run_half("off");
  const Half irrigated = run_half("on");

  const double dryland = rain_fed.grown_kg_dm_per_ha();
  const double watered = irrigated.grown_kg_dm_per_ha();
  ASSERT_GT(dryland, 0.0);

  const double ratio = watered / dryland;
  const double applied_mm = irrigated.run.irrigation.effective_mm;
  const tests::WinchmoreResponse measured = tests::winchmore_wet_treatments(data_dir());

  // Reported, never gated: a rank among fifty treatment-years from one trial on
  // one soil is a description of that sample and not a property of Canterbury.
  std::cout << "  rain-fed          " << dryland << " kg DM/ha\n"
            << "  irrigated         " << watered << " kg DM/ha\n"
            << "  response ratio    " << ratio << "\n"
            << "  measured band     " << kIrrigationResponseLow << " to " << kIrrigationResponseHigh
            << " (p10-p90 of " << measured.treatment_years() << " treatment-years, median "
            << measured.percentile(50.0) << ")\n"
            << "  percentile        " << measured.percentile_of(ratio) << "\n"
            << "  water applied     " << applied_mm << " mm\n"
            << "  response per mm   " << (watered - dryland) / applied_mm << " kg DM/ha/mm\n";

  EXPECT_GE(ratio, kIrrigationResponseLow)
      << "irrigation bought less than Winchmore's driest measured tenth";
  EXPECT_LE(ratio, kIrrigationResponseHigh)
      << "irrigation bought more than Winchmore's wettest measured tenth - the model is "
         "over-responding to water";

  // **A ceiling, not a band.** Martin et al. (2006) put irrigated Canterbury
  // ryegrass and clover near 20 kg DM/ha per mm on total water use. A model
  // that converted applied water faster than the published figure would be
  // over-responding whatever the ratio said, and there is no lower bound worth
  // asserting: a model can always water a farm that did not need it.
  EXPECT_LE((watered - dryland) / applied_mm, 20.0)
      << "more dry matter per mm applied than Martin et al. (2006) measured";
}

// The rain-fed arm has its own published check, and it has to keep passing for
// the ratio above to mean anything: a response measured against a denominator
// that had drifted would be arithmetic rather than evidence.
TEST(IrrigationResponseTest, TheRainFedArmStaysInsideTheMeasuredDrylandRange) {
  const double dryland = run_half("off").grown_kg_dm_per_ha();

  // Winchmore's measured dryland span over the same twenty-five years.
  const test::CalibrationTable table(data_dir() + "/calibration/winchmore-annual-production.csv");
  double lowest = 0.0;
  double highest = 0.0;
  for (std::size_t row = 0; row < table.size(); ++row) {
    const double value = table.number(row, "dryland");
    lowest = (row == 0 || value < lowest) ? value : lowest;
    highest = (row == 0 || value > highest) ? value : highest;
  }

  EXPECT_GE(dryland, lowest) << "drier than any year the trial recorded";
  EXPECT_LE(dryland, highest) << "wetter than any year the trial recorded";
}

// ---------------------------------------------------------------------------
// The regression guard. **Not a validation pass.**

// **This asserts nothing about the world.** The model puts too much of its year
// in summer - E84 measured the gap and E88 measured it again on this bundle at
// 1.49 times Winchmore's 18.0% - and no gate here can make that right. What it
// can do is stop it getting worse without anybody noticing, which is what a
// regression guard is for.
//
// The bound is today's value with headroom, not a target. Passing it is not
// evidence of anything; failing it means something upstream moved the season
// and the person who moved it should say so.
TEST(IrrigationResponseTest, TheSummerBiasDoesNotWorsen) {
  const double share = summer_share(run_half("off").run);

  std::cout << "  rain-fed summer share  " << 100.0 * share << "%\n"
            << "  Winchmore measured     18.0%\n"
            << "  regression ceiling     32.0% (a guard, not a validation band)\n";

  // 0.267 on the day this was written, against a measured 0.180. Five points of
  // headroom: enough that weather and ordinary model movement do not trip it,
  // tight enough that a change which made summer materially worse would.
  EXPECT_LE(share, 0.32) << "the summer share has grown past its regression guard; this is not a "
                            "validation failure, it is a change somebody should account for";

  // And a floor, because a change that moved growth out of summer without
  // anybody intending it is equally worth a conversation - most obviously if
  // somebody "fixes" the season by tuning rather than by phenology.
  EXPECT_GE(share, 0.15) << "the summer share has fallen below its regression guard";
}

}  // namespace
}  // namespace paddock::config
