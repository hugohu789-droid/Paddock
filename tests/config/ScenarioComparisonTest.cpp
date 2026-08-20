// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

#include <paddock/config/ScenarioComparison.hpp>
#include <paddock/core/BudgetLedger.hpp>

namespace paddock::config {
namespace {

/// Finds a row by name, so a test says which quantity it means rather than
/// counting down the table.
const MetricRow& row_named(const ComparisonTable& table, const std::string& name) {
  for (const MetricRow& row : table.metrics) {
    if (row.name == name) {
      return row;
    }
  }
  ADD_FAILURE() << "no row called '" << name << "'";
  static const MetricRow kMissing;
  return kMissing;
}

/// A run with the few numbers these tests read, and nothing else.
///
/// Built by hand rather than simulated: what is under test is the table, and a
/// table fed by a year of weather would fail for reasons that have nothing to
/// do with it. The comparison against a real pair of runs is a validation test
/// and lives with the others.
ComparedScenario made(std::string name, double grown, double irrigation_mm, int events,
                      int stressed_days) {
  ComparedScenario scenario;
  scenario.name = std::move(name);
  scenario.hectares = 80.0;
  scenario.summary.ledger.record_inflow(core::Budget::DryMatter, "pasture_growth", grown);
  scenario.summary.ledger.record_inflow(core::Budget::Water, "rainfall", 640.0);
  scenario.summary.irrigation.events = events;
  scenario.summary.irrigation.effective_mm = irrigation_mm;
  scenario.summary.irrigation.pumped_m3_per_ha = irrigation_mm * 10.0;
  scenario.summary.water_stress.assign(366, 1.0);
  for (int day = 0; day < stressed_days; ++day) {
    scenario.summary.water_stress[static_cast<std::size_t>(day)] = 0.4;
  }
  return scenario;
}

// **A day is stressed when the model says growth was held back, not when it
// crosses a number somebody picked.** FAO-56 Eq. 84 sits at exactly one while
// the root zone still holds readily available water, so anything below it is
// the model's own statement.
TEST(ScenarioComparisonTest, StressedDaysAreTheDaysBelowOne) {
  RunSummary summary;
  summary.water_stress = {1.0, 1.0, 0.999, 0.5, 1.0, 0.0};
  EXPECT_EQ(summary.days_water_stressed(), 3);

  RunSummary never;
  never.water_stress.assign(366, 1.0);
  EXPECT_EQ(never.days_water_stressed(), 0);
}

// The table carries every scenario, in the order it was given, because the
// first is the one the summary reads everything else against.
TEST(ScenarioComparisonTest, TheTableKeepsTheOrderItWasGiven) {
  const ComparisonTable table = compare({made("Rain-fed", 9000.0, 0.0, 0, 60),
                                         made("Irrigated", 12000.0, 369.0, 20, 12),
                                         made("Heavily irrigated", 12600.0, 520.0, 28, 4)});
  ASSERT_EQ(table.scenarios.size(), 3U);
  EXPECT_EQ(table.scenarios[0], "Rain-fed");
  EXPECT_EQ(table.scenarios[2], "Heavily irrigated");
}

// Numbers come out of the run's own ledger, so the table cannot disagree with
// the model it is reporting.
TEST(ScenarioComparisonTest, TheNumbersAreTheOnesTheRunRecorded) {
  const ComparisonTable table =
      compare({made("Rain-fed", 9000.0, 0.0, 0, 60), made("Irrigated", 12000.0, 369.0, 20, 12)});

  EXPECT_DOUBLE_EQ(row_named(table, "Pasture grown").values[0], 9000.0);
  EXPECT_DOUBLE_EQ(row_named(table, "Pasture grown").values[1], 12000.0);
  EXPECT_DOUBLE_EQ(row_named(table, "Irrigation applied").values[1], 369.0);
  EXPECT_DOUBLE_EQ(row_named(table, "Irrigations").values[0], 0.0);
  EXPECT_EQ(row_named(table, "Days growth held back by dry soil").values[0], 60.0);
  EXPECT_EQ(row_named(table, "Days growth held back by dry soil").values[1], 12.0);

  // One millimetre over one hectare is ten cubic metres, so 369 mm over 80 ha
  // is 295.2 ML. Arithmetic rather than a model, and worth pinning: a table
  // that got this wrong would misstate a consent by a factor of ten.
  EXPECT_NEAR(row_named(table, "Water pumped").values[1], 295.2, 0.05);
}

// **A header that does not say what differs invites the reader to assume it was
// one thing when it was three.** Settings every scenario shares are dropped;
// the ones that differ are kept.
TEST(ScenarioComparisonTest, OnlyTheSettingsThatDifferAreShown) {
  ComparedScenario dry = made("Rain-fed", 9000.0, 0.0, 0, 60);
  dry.settings = {{"Farm", "lincoln-lurdf"}, {"Head", "417"}, {"Irrigation", "off"}};
  ComparedScenario wet = made("Irrigated", 12000.0, 369.0, 20, 12);
  wet.settings = {{"Farm", "lincoln-lurdf"}, {"Head", "417"}, {"Irrigation", "below 50%"}};

  const ComparisonTable table = compare({dry, wet});
  ASSERT_EQ(table.differences.size(), 1U);
  EXPECT_EQ(table.differences.front().name, "Irrigation");
  EXPECT_EQ(table.differences.front().values[0], "off");
  EXPECT_EQ(table.differences.front().values[1], "below 50%");
}

// A setting one scenario named and another did not is a difference, not a
// blank: it means that scenario was set up another way.
TEST(ScenarioComparisonTest, AMissingSettingCountsAsADifference) {
  ComparedScenario dry = made("Rain-fed", 9000.0, 0.0, 0, 60);
  dry.settings = {{"Farm", "lincoln-lurdf"}};
  ComparedScenario wet = made("Irrigated", 12000.0, 369.0, 20, 12);
  wet.settings = {{"Farm", "lincoln-lurdf"}, {"Refill to", "85%"}};

  const ComparisonTable table = compare({dry, wet});
  ASSERT_EQ(table.differences.size(), 1U);
  EXPECT_EQ(table.differences.front().name, "Refill to");
  EXPECT_EQ(table.differences.front().values[0], "-");
}

// **Nitrogen leaching is not modelled, and the table says so.** Three nitrogen
// rows with nothing said about loss reads as a farm that loses none, which is
// not a result - it is a process nothing here computes.
TEST(ScenarioComparisonTest, TheTableSaysWhatItCannotSay) {
  const ComparisonTable table =
      compare({made("Rain-fed", 9000.0, 0.0, 0, 60), made("Irrigated", 12000.0, 369.0, 20, 12)});

  ASSERT_FALSE(table.caveats.empty());
  const bool mentions_leaching =
      std::any_of(table.caveats.begin(), table.caveats.end(), [](const std::string& caveat) {
        return caveat.find("leaching") != std::string::npos;
      });
  EXPECT_TRUE(mentions_leaching);
}

// The summary states differences and does not recommend. A recommendation needs
// a price for water and a price for feed, and this model has neither.
TEST(ScenarioComparisonTest, TheSummaryStatesDifferencesAndRecommendsNothing) {
  const ComparisonTable table =
      compare({made("Rain-fed", 9000.0, 0.0, 0, 60), made("Irrigated", 12000.0, 369.0, 20, 12)});
  const std::string words = summarise(table);

  EXPECT_NE(words.find("Irrigated against Rain-fed"), std::string::npos);
  EXPECT_NE(words.find("+3000"), std::string::npos) << "the growth difference should be stated";
  EXPECT_NE(words.find("+33%"), std::string::npos) << "and as a share of the base";
  EXPECT_NE(words.find("-48 days"), std::string::npos) << "48 fewer stressed days";

  // Nothing that reads as advice.
  for (const std::string& advice : {"should", "recommend", "best", "better choice"}) {
    EXPECT_EQ(words.find(advice), std::string::npos) << "found '" << advice << "' in the summary";
  }
  EXPECT_NE(words.find("neither price is in this model"), std::string::npos);
}

// One scenario is not a comparison, and saying so beats printing a paragraph
// about nothing.
TEST(ScenarioComparisonTest, OneScenarioIsNotAComparison) {
  const ComparisonTable table = compare({made("Rain-fed", 9000.0, 0.0, 0, 60)});
  EXPECT_NE(summarise(table).find("needs two scenarios"), std::string::npos);
}

// Markdown and CSV carry the same numbers. Two renderings that disagreed would
// be two answers, which is the thing this whole module exists to avoid.
TEST(ScenarioComparisonTest, BothRenderingsCarryTheSameNumbers) {
  ComparedScenario dry = made("Rain-fed", 9000.0, 0.0, 0, 60);
  dry.settings = {{"Irrigation", "off"}};
  ComparedScenario wet = made("Irrigated", 12000.0, 369.0, 20, 12);
  wet.settings = {{"Irrigation", "below 50%"}};

  const ComparisonTable table = compare({dry, wet});
  const std::string markdown = as_markdown(table);
  const std::string csv = as_csv(table);

  for (const std::string& expected : {"Rain-fed", "Irrigated", "9000", "12000", "369", "295.2"}) {
    EXPECT_NE(markdown.find(expected), std::string::npos) << "markdown is missing " << expected;
    EXPECT_NE(csv.find(expected), std::string::npos) << "csv is missing " << expected;
  }
  EXPECT_NE(markdown.find("| ---: |"), std::string::npos) << "numbers should be right aligned";
  EXPECT_NE(markdown.find("leaching"), std::string::npos) << "caveats belong under the table";
}

// A name with a comma in it must not split a CSV row into two.
TEST(ScenarioComparisonTest, ACommaInAScenarioNameDoesNotBreakTheCsv) {
  const ComparisonTable table = compare(
      {made("Dry, as now", 9000.0, 0.0, 0, 60), made("Irrigated", 12000.0, 369.0, 20, 12)});
  const std::string csv = as_csv(table);
  EXPECT_NE(csv.find("\"Dry, as now\""), std::string::npos);
}

}  // namespace
}  // namespace paddock::config
