// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

/// The indicator layer, and the economics file it finally reads.
///
/// **A dashboard is the easiest place in a project like this to undo its own
/// discipline.** A tile reading "27.3 kg N/ha" is read as a measurement, and it
/// is a model output resting on a placeholder patch uptake that excludes about
/// 15% of the real loss. So most of what these tests check is not the
/// arithmetic - it is that the caveat travels with the number, into the page and
/// into every export.

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

#include <paddock/config/EconomicsConfig.hpp>
#include <paddock/config/FarmDashboard.hpp>
#include <paddock/config/NitrogenReport.hpp>
#include <paddock/config/ScenarioRun.hpp>

#include "../support/ShippedBundle.hpp"
#include "../support/ValueOf.hpp"

namespace paddock::config {
namespace {

std::string data_path(const std::string& relative) {
  return std::string(PADDOCK_DATA_DIR) + "/" + relative;
}

core::DietQuality pasture_diet() {
  core::DietQuality diet;
  diet.metabolisable_energy_mj_per_kg_dm = 10.5;
  diet.digestibility_percent = 75.0;
  return diet;
}

ScenarioBundle a_bundle() {
  return tests::load_on_flat_ground(data_path("scenarios/lincoln-lurdf"));
}

// ---------------------------------------------------------------- economics

// **The eighth "implemented and read by nothing", and the first to be closed by
// making something read it.** Every cost in the economics file came from Beef +
// Lamb New Zealand's Class 6 survey and was cited line by line - and was then
// typed by hand into the tests that needed it, so the twenty-two figures a
// farm's year rests on lived in two places and only one had a source.
TEST(DashboardTest, TheEconomicsFileIsLoadedAndCarriesItsProvenance) {
  const FarmEconomics economics = load_economics(data_path("economics/canterbury-sheep.toml"));

  EXPECT_EQ(economics.name, "canterbury_sheep");
  EXPECT_EQ(economics.survey_class, "Class 6 S.I. Finishing Breeding");

  // The survey's own figures, to the cent.
  EXPECT_DOUBLE_EQ(economics.costs.fertiliser, 153.62);
  EXPECT_DOUBLE_EQ(economics.costs.wages_and_salaries, 87.31);
  EXPECT_DOUBLE_EQ(economics.costs.rates, 45.01);

  // **Interest, rent and depreciation are carried and not charged.** They are
  // real costs and they are not operating costs, and the file says which.
  EXPECT_DOUBLE_EQ(economics.costs.interest, 219.18);
  EXPECT_FALSE(economics.costs.charge_interest);
  EXPECT_FALSE(economics.costs.charge_rent);
  EXPECT_FALSE(economics.costs.charge_depreciation);

  // The annual total excludes them, which is what makes it comparable with the
  // survey's own EBITRm.
  EXPECT_NEAR(economics.costs.annual_per_hectare(), 927.86, 0.01);

  // **The prices are weaker than the costs and the file says so.** A whole-farm
  // margin is only as good as its worst line, which is what weakest_status is
  // for.
  EXPECT_EQ(economics.weakest_status(), Provenance::Placeholder)
      << "the cull ewe price has no source, so nothing computed from it is evidenced";

  const auto has = [&economics](const std::string& name, Provenance status) {
    return std::any_of(
        economics.provenance.begin(), economics.provenance.end(),
        [&](const auto& entry) { return entry.first == name && entry.second.status == status; });
  };
  EXPECT_TRUE(has("fertiliser", Provenance::Direct));
  EXPECT_TRUE(has("lamb_dollars_per_kg_carcass", Provenance::Verify));
  EXPECT_TRUE(has("cull_ewe_dollars_per_head", Provenance::Placeholder));
}

// A misspelled cost line has to be an error. A farm quietly missing its
// fertiliser bill still balances its books; it just reports a margin nobody
// could achieve.
TEST(DashboardTest, AnEconomicsFileMissingACostLineIsRefused) {
  EXPECT_THROW(static_cast<void>(load_economics(data_path("economics"))), std::exception)
      << "a directory is not an economics file";
}

// The business a bundle and an economics file make together - one place, so the
// command line, the window and the tests describe the same farm.
TEST(DashboardTest, ABusinessComesFromTheBundleAndTheEconomics) {
  const ScenarioBundle bundle = a_bundle();
  const FarmEconomics economics = load_economics(data_path("economics/canterbury-sheep.toml"));

  const FarmBusiness business = business_from(bundle, economics);

  EXPECT_DOUBLE_EQ(business.costs.fertiliser, economics.costs.fertiliser);
  EXPECT_DOUBLE_EQ(business.prices.lamb_dollars_per_kg_carcass,
                   economics.prices.lamb_dollars_per_kg_carcass);

  // Opening cash is per hectare, so it scales with the farm rather than being a
  // number that happens to suit one block. 80 ha at $400.
  EXPECT_NEAR(business.opening_balance_dollars, 400.0 * 80.0, 1.0);

  // **The flock has an age structure**, which is the point of splitting the
  // bundle's head count across the breeding classes at all.
  EXPECT_GT(business.flock.head(), 0);
  EXPECT_EQ(static_cast<int>(business.flock.cohorts().size()), business.rates.cull_age_years - 1);
  for (const core::AgeCohort& cohort : business.flock.cohorts()) {
    EXPECT_GE(cohort.age_years, 2);
    EXPECT_LE(cohort.age_years, business.rates.cull_age_years);
  }
}

// ---------------------------------------------------------------- dashboard

FarmDashboard a_dashboard(bool with_money) {
  const ScenarioBundle bundle = a_bundle();
  const std::optional<NitrogenRegulation> rule =
      load_nitrogen_regulation(data_path("regulations/canterbury-nitrogen.toml"));

  if (!with_money) {
    return build_dashboard(
        bundle,
        run_managed_scenario(bundle, tests::value_of(bundle.management, "a [management] section"),
                             pasture_diet(), "plain"),
        "plain", rule);
  }
  const FarmEconomics economics = load_economics(data_path("economics/canterbury-sheep.toml"));
  return build_dashboard(
      bundle,
      run_managed_scenario(bundle, tests::value_of(bundle.management, "a [management] section"),
                           pasture_diet(), "priced", business_from(bundle, economics)),
      "priced", rule);
}

TEST(DashboardTest, EveryIndicatorSaysHowFarItCanBeTrusted) {
  const FarmDashboard board = a_dashboard(true);

  ASSERT_FALSE(board.panels.empty());
  for (const Indicator& indicator : board.all_indicators()) {
    EXPECT_FALSE(indicator.name.empty());
    EXPECT_FALSE(indicator.unit.empty()) << indicator.name;

    // **A note is not optional where the trust is weak.** A placeholder without
    // one is a number a reader has no way to discount, and a fitted value has
    // to name what it was fitted to.
    if (indicator.trust == Provenance::Placeholder || indicator.trust == Provenance::Fitted) {
      EXPECT_FALSE(indicator.note.empty())
          << indicator.name << " is " << to_string(indicator.trust) << " and says nothing about it";
    }
  }

  EXPECT_GT(board.indicators_total(), 15);
  EXPECT_GT(board.indicators_on_evidence(), 0);
  EXPECT_LT(board.indicators_on_evidence(), board.indicators_total())
      << "a dashboard where everything was evidenced would be one that had stopped counting";
}

// **The panel that says how much of the rest means anything** goes into the
// page, not just into the struct.
TEST(DashboardTest, ThePageCountsWhatRestsOnEvidence) {
  const FarmDashboard board = a_dashboard(true);
  const std::string page = as_text(board);

  EXPECT_NE(page.find("How much of this can be trusted"), std::string::npos);
  EXPECT_NE(page.find(std::to_string(board.indicators_on_evidence()) + " of " +
                      std::to_string(board.indicators_total())),
            std::string::npos);
  EXPECT_NE(page.find("verify.md"), std::string::npos)
      << "and where to look up what each placeholder would take";

  // Counts print as counts. "23.00 head" is two ewes' worth of spurious
  // precision on a number that cannot be fractional.
  EXPECT_EQ(page.find(".00 head"), std::string::npos);
  EXPECT_EQ(page.find(".00 days"), std::string::npos);
}

// **The trust column is not optional in the export either**: a spreadsheet that
// dropped it would be the same number with its caveat removed.
TEST(DashboardTest, TheIndicatorExportCarriesTheTrustAndTheBand) {
  const FarmDashboard board = a_dashboard(true);
  const std::string csv = indicators_as_csv(board);

  EXPECT_NE(csv.find("farm,year,panel,indicator,value,unit,standing,low,high,trust,note"),
            std::string::npos);
  EXPECT_NE(csv.find("placeholder"), std::string::npos);
  EXPECT_NE(csv.find("fitted"), std::string::npos);

  // One header plus one row per indicator, and every note quoted - they contain
  // commas.
  const auto rows = static_cast<int>(std::count(csv.begin(), csv.end(), '\n'));
  EXPECT_EQ(rows, board.indicators_total() + 1);
  EXPECT_NE(csv.find('"'), std::string::npos);
}

TEST(DashboardTest, TheSeriesExportHasADateColumnAndOneColumnPerSeries) {
  const FarmDashboard board = a_dashboard(false);
  const std::string csv = series_as_csv(board);

  ASSERT_FALSE(board.series.empty());
  EXPECT_EQ(csv.compare(0, 4, "date"), 0);
  for (const DashboardSeries& series : board.series) {
    EXPECT_NE(csv.find(series.name + " (" + series.unit + ")"), std::string::npos) << series.name;
  }

  // **The level a series is read against travels with it.** A cover column
  // exported without it is a column somebody plots and then guesses at: 1,400
  // kg DM/ha is a farm in trouble or a farm doing exactly what it planned.
  const auto with_reference =
      std::count_if(board.series.begin(), board.series.end(),
                    [](const DashboardSeries& series) { return series.reference.has_value(); });
  ASSERT_GT(with_reference, 0) << "the cover series is held to the management minimum";
  EXPECT_NE(csv.find("[held at "), std::string::npos);

  // A row per day, plus the header.
  const auto rows = static_cast<int>(std::count(csv.begin(), csv.end(), '\n'));
  EXPECT_EQ(rows, static_cast<int>(board.dates.size()) + 1);
}

// **Money and flock panels are absent rather than blank** when nobody stated
// what the farm spends. Showing a farm's finances against costs nobody supplied
// would be worse than showing nothing.
TEST(DashboardTest, AnUnpricedRunHasNoMoneyPanelAtAll) {
  const FarmDashboard priced = a_dashboard(true);
  const FarmDashboard plain = a_dashboard(false);

  const auto has_panel = [](const FarmDashboard& board, const std::string& title) {
    return std::any_of(board.panels.begin(), board.panels.end(),
                       [&title](const DashboardPanel& panel) { return panel.title == title; });
  };

  EXPECT_TRUE(has_panel(priced, "Money"));
  EXPECT_TRUE(has_panel(priced, "The flock"));
  EXPECT_FALSE(has_panel(plain, "Money"));
  EXPECT_FALSE(has_panel(plain, "The flock"));

  // The pasture, water and nitrogen panels do not need economics.
  EXPECT_TRUE(has_panel(plain, "The year"));
  EXPECT_TRUE(has_panel(plain, "Water"));
  EXPECT_TRUE(has_panel(plain, "Environment"));
}

// Without a zone rule the nitrogen panel still reports what leached - it just
// has nothing to say about compliance, which is honest: New Zealand sets these
// catchment by catchment.
TEST(DashboardTest, WithoutARuleThereIsNoComplianceClaim) {
  const ScenarioBundle bundle = a_bundle();
  const FarmDashboard board = build_dashboard(
      bundle,
      run_managed_scenario(bundle, tests::value_of(bundle.management, "a [management] section"),
                           pasture_diet(), "no rule"),
      "no rule");

  for (const Indicator& indicator : board.all_indicators()) {
    if (indicator.name == "Nitrate leached") {
      EXPECT_EQ(indicator.standing, Standing::Unmeasured);
      EXPECT_NE(indicator.note.find("catchment by catchment"), std::string::npos);
      return;
    }
  }
  FAIL() << "the nitrogen indicator should be there whether or not a rule is";
}

// The comparison one dashboard cannot make.
TEST(DashboardTest, YearsCompareIndicatorByIndicator) {
  std::vector<FarmDashboard> boards;
  for (const int start : {2015, 2024}) {
    ScenarioBundle one = a_bundle();
    one.range = core::DateRange{core::Date{start, 7, 1}, core::Date{start + 1, 6, 30}};
    const std::string label = std::to_string(start);
    boards.push_back(build_dashboard(
        one,
        run_managed_scenario(one, tests::value_of(one.management, "a [management] section"),
                             pasture_diet(), label),
        label));
  }

  const std::string table = compare_dashboards_as_text(boards);
  EXPECT_NE(table.find("2015"), std::string::npos);
  EXPECT_NE(table.find("2024"), std::string::npos);
  EXPECT_NE(table.find("Pasture grown"), std::string::npos);
  EXPECT_NE(table.find("outside its band"), std::string::npos)
      << "the legend, or a reader cannot tell what the marks mean";

  const std::string csv = compare_dashboards_as_csv(boards);
  EXPECT_EQ(static_cast<int>(std::count(csv.begin(), csv.end(), '\n')),
            boards.front().indicators_total() + boards.back().indicators_total() + 1);
}

}  // namespace
}  // namespace paddock::config
