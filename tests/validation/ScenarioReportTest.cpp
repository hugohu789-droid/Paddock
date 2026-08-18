// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

// The report a run produces.
//
// A report is the point at which a model stops being code and starts being a
// claim somebody acts on, so what is tested here is mostly whether it tells the
// truth about itself: that it says when feed was bought, that it says when the
// stock went short, and that it carries the evidence caveats rather than
// presenting numbers as findings.

#include <gtest/gtest.h>

#include <fstream>
#include <string>

#include <paddock/config/ScenarioReport.hpp>

namespace paddock::config {
namespace {

std::string bundle_path() {
  return std::string(PADDOCK_DATA_DIR) + "/scenarios/canterbury-grazed";
}

core::DietQuality pasture_diet() {
  core::DietQuality diet;
  diet.metabolisable_energy_mj_per_kg_dm = 10.5;
  diet.digestibility_percent = 75.0;
  return diet;
}

core::ManagementPolicy policy() {
  core::ManagementPolicy chosen;
  chosen.minimum_cover_kg_dm_per_ha = 1600.0;
  chosen.rotation_cover_threshold_kg_dm_per_ha = 2200.0;
  return chosen;
}

bool contains(const std::string& text, const std::string& needle) {
  return text.find(needle) != std::string::npos;
}

TEST(ScenarioReportTest, ItReportsEveryPurchaseWithItsDate) {
  ScenarioBundle bundle = load_scenario(bundle_path());
  bundle.mobs.front().head = 1400;
  const RunSummary run = run_managed_scenario(bundle, policy(), pasture_diet(), "managed");
  ASSERT_FALSE(run.purchases.empty());

  const std::string report = render_report(bundle, run, {"Canterbury demonstration", nullptr});

  EXPECT_TRUE(contains(report, "## Bought feed"));
  EXPECT_TRUE(contains(report, "kg DM"));
  // The first purchase's date has to appear somewhere: a report that gave only
  // a total would not answer when the farm ran out.
  EXPECT_TRUE(contains(report, run.purchases.front().date.to_iso_string()))
      << "the report does not say when feed was bought";
  EXPECT_TRUE(contains(report, run.purchases.front().mob_name));
}

// A farm that bought nothing says so, rather than leaving an empty table for a
// reader to interpret.
TEST(ScenarioReportTest, AFarmThatBoughtNothingSaysSo) {
  ScenarioBundle bundle = load_scenario(bundle_path());
  bundle.mobs.front().head = 300;
  const RunSummary run = run_managed_scenario(bundle, policy(), pasture_diet(), "lightly stocked");
  ASSERT_TRUE(run.purchases.empty());

  const std::string report = render_report(bundle, run);
  EXPECT_TRUE(contains(report, "None. The farm carried its stock on what it grew."));
}

// The caveats travel with the numbers. This is the test that stops a
// good-looking report from being a misleading one.
TEST(ScenarioReportTest, TheEvidenceCaveatsTravelWithTheNumbers) {
  const ScenarioBundle bundle = load_scenario(bundle_path());
  const RunSummary run = run_managed_scenario(bundle, policy(), pasture_diet(), "managed");
  const std::string report = render_report(bundle, run);

  EXPECT_TRUE(contains(report, "What this report may be relied on for"));
  EXPECT_TRUE(contains(report, "Overstated by up to 17%"))
      << "the sheep carrying capacity caveat is missing";
  EXPECT_TRUE(contains(report, "Not quotable"))
      << "the report does not say absolute liveweight gain cannot be quoted";
  EXPECT_TRUE(contains(report, "lactation is absent"));
  EXPECT_TRUE(contains(report, "docs/verify.md"));

  // The ground the run was over. Until a [terrain] section existed every farm
  // was flat and nothing said so, which made the slope equations look like they
  // were in play when they had never once run.
  EXPECT_TRUE(contains(report, "modelled flat"))
      << "the report does not say the ground was flat, and this bundle's is";
  EXPECT_TRUE(contains(report, "Anything to do with slope"))
      << "the report does not say the slope terms did not apply";
}

// The pasture and the stock, month by month, because a year is not one number.
TEST(ScenarioReportTest, ItShowsTheYearMonthByMonth) {
  const ScenarioBundle bundle = load_scenario(bundle_path());
  const RunSummary run = run_managed_scenario(bundle, policy(), pasture_diet(), "managed");
  const std::string report = render_report(bundle, run);

  EXPECT_TRUE(contains(report, "## The pasture"));
  EXPECT_TRUE(contains(report, "## The stock"));
  EXPECT_TRUE(contains(report, "July"));
  EXPECT_TRUE(contains(report, "December"));
  EXPECT_TRUE(contains(report, "## Did the books balance"));
  EXPECT_TRUE(contains(report, "Every kilogram and every millimetre is accounted for"))
      << "the budgets did not close, or the report failed to notice";
}

// The comparison report, which is the one a farmer would actually read.
TEST(ScenarioReportTest, TheComparisonPutsFeedBoughtBesideConditionHeld) {
  ScenarioBundle bundle = load_scenario(bundle_path());
  bundle.mobs.front().head = 1400;

  core::ManagementPolicy may_not_buy = policy();
  may_not_buy.may_buy_feed = false;

  const RunSummary bought = run_managed_scenario(bundle, policy(), pasture_diet(), "buying feed");
  const RunSummary went_without =
      run_managed_scenario(bundle, may_not_buy, pasture_diet(), "no bought feed");

  const std::string report = render_comparison_report(bundle, bought, went_without);

  EXPECT_TRUE(contains(report, "buying feed"));
  EXPECT_TRUE(contains(report, "no bought feed"));
  EXPECT_TRUE(contains(report, "Feed bought in"));
  EXPECT_TRUE(contains(report, "Liveweight change"));
  EXPECT_TRUE(contains(report, "has not saved anything"))
      << "the report does not warn that saving feed by losing condition saves nothing";
}

// Written out so a person can read one, and so CI keeps it.
TEST(ScenarioReportTest, WritesAReportForInspection) {
  ScenarioBundle bundle = load_scenario(bundle_path());
  bundle.mobs.front().head = 1400;
  const core::ManagementPolicy chosen = policy();
  const RunSummary run = run_managed_scenario(bundle, chosen, pasture_diet(), "managed");

  ReportOptions options;
  options.farm_name = "Canterbury demonstration block";
  options.policy = &chosen;

  const std::string path = std::string(PADDOCK_VALIDATION_OUTPUT_DIR) + "/farm-report.md";
  std::ofstream out(path);
  ASSERT_TRUE(out);
  out << render_report(bundle, run, options);
  out.close();

  GTEST_LOG_(INFO) << "wrote " << path;
}

}  // namespace
}  // namespace paddock::config
