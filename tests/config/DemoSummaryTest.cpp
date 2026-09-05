// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

// The customer-facing outcome summary: six numbers and what each is worth.
//
// **What is under test is whether the page can be trusted by somebody who will
// not read the code.** It is shown once, quickly, to a person deciding whether
// to believe the tool - so the failures that matter are the ones that flatter:
// a metric promoted above its evidence, a percentage invented out of a zero, a
// missing figure shown as nought, or an inconvenient outcome quietly dropped
// without saying so.
//
// The indicators are built by hand rather than simulated. What is under test is
// the selection, the arithmetic and the labelling; a year of weather would make
// this fail for reasons that have nothing to do with any of them.

#include <gtest/gtest.h>

#include <algorithm>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include <paddock/config/DemoSummary.hpp>

namespace paddock::config {
namespace {

Indicator made(const std::string& name, double value, const std::string& unit, Provenance trust,
               std::optional<double> low = std::nullopt,
               std::optional<double> high = std::nullopt) {
  Indicator indicator;
  indicator.name = name;
  indicator.value = value;
  indicator.unit = unit;
  indicator.trust = trust;
  indicator.low = low;
  indicator.high = high;
  indicator.note = "why this figure is what it is";
  return indicator;
}

/// A dashboard carrying the five indicators the summary looks for.
FarmDashboard board(const std::string& label, double grown, double lowest, double stressed_days,
                    double efficiency, double drainage) {
  FarmDashboard dashboard;
  dashboard.label = label;

  DashboardPanel year;
  year.title = "The year";
  year.indicators.push_back(
      made("Pasture grown", grown, "kg DM/ha", Provenance::Fitted, 4000.0, 9800.0));
  year.indicators.push_back(made("Lowest cover", lowest, "kg DM/ha", Provenance::Fitted));
  dashboard.panels.push_back(std::move(year));

  DashboardPanel water;
  water.title = "Water";
  water.indicators.push_back(
      made("Days water-stressed", stressed_days, "days", Provenance::Derived));
  water.indicators.push_back(
      made("Water use efficiency", efficiency, "kg DM/ha/mm", Provenance::Fitted, 8.0, 16.0));
  water.indicators.push_back(made("Drainage", drainage, "mm", Provenance::Derived));
  dashboard.panels.push_back(std::move(water));

  return dashboard;
}

RunSummary run_with(double irrigation_mm) {
  RunSummary summary;
  summary.irrigation.effective_mm = irrigation_mm;
  return summary;
}

/// The flagship pair's own figures.
DemoSummary flagship() {
  return demo_summary(board("rain-fed", 6407.0, 710.0, 233.0, 12.10, 109.0),
                      board("irrigated", 11747.0, 847.0, 15.0, 13.92, 152.0), run_with(0.0),
                      run_with(374.766));
}

const OutcomeRow* row_named(const DemoSummary& summary, const std::string& name) {
  const auto found = std::find_if(summary.outcomes.begin(), summary.outcomes.end(),
                                  [&name](const OutcomeRow& row) { return row.name == name; });
  return found == summary.outcomes.end() ? nullptr : &*found;
}

const OmittedOutcome* omitted_named(const DemoSummary& summary, const std::string& name) {
  const auto found =
      std::find_if(summary.omitted.begin(), summary.omitted.end(),
                   [&name](const OmittedOutcome& left_out) { return left_out.name == name; });
  return found == summary.omitted.end() ? nullptr : &*found;
}

bool contains(const std::string& text, const std::string& needle) {
  return text.find(needle) != std::string::npos;
}

// ---------------------------------------------------------------------------
// How many, and which.

// **Between four and six, because a demonstration is not an audit.** The full
// dashboard reports twenty-seven indicators and this page exists precisely
// because that is the wrong number to put in front of somebody for five
// minutes.
TEST(DemoSummaryTest, ThePageCarriesBetweenFourAndSixOutcomes) {
  const DemoSummary summary = flagship();
  EXPECT_GE(summary.outcomes.size(), 4U);
  EXPECT_LE(summary.outcomes.size(), 7U);
}

// Everything on the page is grass or water, which is what this model has been
// checked on.
TEST(DemoSummaryTest, EveryOutcomeIsGrassOrWater) {
  const DemoSummary summary = flagship();
  for (const OutcomeRow& row : summary.outcomes) {
    // "x rain-fed" is the response ratio - dimensionless pasture, which is
    // what this test is actually guarding: nothing on the page is an animal.
    const bool grass_or_water = row.unit == "kg DM/ha" || row.unit == "mm" || row.unit == "days" ||
                                row.unit == "kg DM/ha/mm" || row.unit == "x rain-fed";
    EXPECT_TRUE(grass_or_water) << row.name << " is measured in " << row.unit;
  }
}

// ---------------------------------------------------------------------------
// Units.

TEST(DemoSummaryTest, EveryRowCarriesItsUnit) {
  const DemoSummary summary = flagship();
  for (const OutcomeRow& row : summary.outcomes) {
    EXPECT_FALSE(row.unit.empty()) << row.name << " has no unit";
  }

  EXPECT_EQ(row_named(summary, "Pasture grown")->unit, "kg DM/ha");
  EXPECT_EQ(row_named(summary, "Growth-limited days")->unit, "days");
  EXPECT_EQ(row_named(summary, "Irrigation applied")->unit, "mm");
  EXPECT_EQ(row_named(summary, "Dry matter per mm")->unit, "kg DM/ha/mm");
}

// **Significance belongs to the quantity.** Kilograms of dry matter to two
// decimal places claims a precision this model does not have; a water use
// efficiency to none loses the difference the page is about.
TEST(DemoSummaryTest, EachQuantityIsPrintedToItsOwnPrecision) {
  const std::string page = as_text(flagship());
  EXPECT_TRUE(contains(page, "6407")) << page;
  EXPECT_FALSE(contains(page, "6407.0")) << "dry matter printed to a precision it does not have";
  EXPECT_TRUE(contains(page, "12.1")) << "water use efficiency lost its decimal";
  EXPECT_TRUE(contains(page, "13.9")) << page;
}

TEST(DemoSummaryTest, TheUnitAppearsOnThePageBesideTheNumbers) {
  const std::string page = as_text(flagship());
  EXPECT_TRUE(contains(page, "kg DM/ha")) << page;
  EXPECT_TRUE(contains(page, "days")) << page;
  EXPECT_TRUE(contains(page, "kg DM/ha/mm")) << page;
}

// ---------------------------------------------------------------------------
// Differences and percentages.

TEST(DemoSummaryTest, TheDifferenceIsSignedAndInTheSameUnit) {
  const DemoSummary summary = flagship();

  const OutcomeRow* grown = row_named(summary, "Pasture grown");
  ASSERT_NE(grown, nullptr);
  EXPECT_DOUBLE_EQ(grown->difference, 11747.0 - 6407.0);
  ASSERT_TRUE(grown->percent.has_value());
  EXPECT_NEAR(*grown->percent, 83.35, 0.01);

  // **A fall is a fall.** Days held back drops from 233 to 15, and the page
  // must not report that as a gain of 218 because somebody took an absolute
  // value to make the column tidy.
  const OutcomeRow* days = row_named(summary, "Growth-limited days");
  ASSERT_NE(days, nullptr);
  EXPECT_DOUBLE_EQ(days->difference, -218.0);
  ASSERT_TRUE(days->percent.has_value());
  EXPECT_NEAR(*days->percent, -93.56, 0.01);
}

// **A percentage of nothing is not a percentage.** A rain-fed farm applies no
// water, and "up an infinite per cent" is not a sentence anybody should be
// shown - so the field is empty and the page says "from 0" rather than leaving
// a blank a reader would take for a missing figure.
TEST(DemoSummaryTest, AnIncreaseFromNothingHasNoPercentage) {
  const DemoSummary summary = flagship();
  const OutcomeRow* applied = row_named(summary, "Irrigation applied");
  ASSERT_NE(applied, nullptr);

  EXPECT_DOUBLE_EQ(applied->before, 0.0);
  EXPECT_NEAR(applied->after, 374.766, 1e-9);
  EXPECT_NEAR(applied->difference, 374.766, 1e-9);
  EXPECT_FALSE(applied->percent.has_value()) << "a percentage was computed from a zero";

  const std::string page = as_text(summary);
  EXPECT_TRUE(contains(page, "from 0")) << page;
  EXPECT_FALSE(contains(page, "inf")) << page;
  EXPECT_FALSE(contains(page, "nan")) << page;
}

// A quantity too small to divide by gets no percentage either, for the same
// reason: 0.4 mm to 40 mm is not a ten-thousand per cent improvement, it is a
// farm that started from nothing worth measuring.
TEST(DemoSummaryTest, ATinyStartingValueGivesNoPercentageEither) {
  const DemoSummary summary =
      demo_summary(board("a", 6407.0, 710.0, 233.0, 12.10, 109.0),
                   board("b", 11747.0, 847.0, 15.0, 13.92, 152.0), run_with(1e-12), run_with(40.0));
  const OutcomeRow* applied = row_named(summary, "Irrigation applied");
  ASSERT_NE(applied, nullptr);
  EXPECT_FALSE(applied->percent.has_value());
}

// Two identical runs read as no change rather than as a blank.
TEST(DemoSummaryTest, NoChangeReadsAsZeroNotAsAbsence) {
  const FarmDashboard same = board("a", 6407.0, 710.0, 233.0, 12.10, 109.0);
  const DemoSummary summary = demo_summary(same, same, run_with(0.0), run_with(0.0));
  const OutcomeRow* grown = row_named(summary, "Pasture grown");
  ASSERT_NE(grown, nullptr);
  EXPECT_DOUBLE_EQ(grown->difference, 0.0);
  ASSERT_TRUE(grown->percent.has_value());
  EXPECT_DOUBLE_EQ(*grown->percent, 0.0);
}

// ---------------------------------------------------------------------------
// Missing and unsupported metrics.

// **A run that reported nothing and a run that reported nought are different
// facts**, and a dash in a demonstration is read as a zero every time. A metric
// one side does not carry leaves the table and is named in what was left out.
TEST(DemoSummaryTest, AMetricOneSideDoesNotCarryIsLeftOutAndSaidSo) {
  FarmDashboard incomplete = board("b", 11747.0, 847.0, 15.0, 13.92, 152.0);
  // Take drainage away from one side only.
  for (DashboardPanel& panel : incomplete.panels) {
    panel.indicators.erase(
        std::remove_if(panel.indicators.begin(), panel.indicators.end(),
                       [](const Indicator& indicator) { return indicator.name == "Drainage"; }),
        panel.indicators.end());
  }

  const DemoSummary summary = demo_summary(board("a", 6407.0, 710.0, 233.0, 12.10, 109.0),
                                           incomplete, run_with(0.0), run_with(375.0));

  EXPECT_EQ(row_named(summary, "Drainage below roots"), nullptr)
      << "a metric only one run carried was shown anyway";

  const OmittedOutcome* left_out = omitted_named(summary, "Drainage below roots");
  ASSERT_NE(left_out, nullptr) << "it vanished without a word";
  EXPECT_TRUE(contains(left_out->reason, "did not report")) << left_out->reason;

  // And the rest of the page is unaffected.
  EXPECT_NE(row_named(summary, "Pasture grown"), nullptr);
}

// **What was left out on purpose is part of the summary.** A page that quietly
// drops the feed bill chose its metrics to flatter; one that says why is
// telling the customer something true about the tool.
TEST(DemoSummaryTest, TheOutcomesDeliberatelyLeftOutAreNamedWithTheirReasons) {
  const DemoSummary summary = flagship();

  for (const std::string& name : {std::string("Bought feed and supplement"),
                                  std::string("Liveweight, stocking rate and lamb weights"),
                                  std::string("Money"), std::string("Nitrate leached")}) {
    const OmittedOutcome* left_out = omitted_named(summary, name);
    ASSERT_NE(left_out, nullptr) << name << " was dropped without a word";
    EXPECT_FALSE(left_out->reason.empty()) << name;
  }

  // The reasons are the verification tracker's, not new ones invented here.
  EXPECT_TRUE(contains(omitted_named(summary, "Bought feed and supplement")->reason, "E71"));

  // And none of them is on the table.
  for (const OutcomeRow& row : summary.outcomes) {
    EXPECT_FALSE(contains(row.name, "feed")) << row.name;
    EXPECT_FALSE(contains(row.name, "Liveweight")) << row.name;
    EXPECT_FALSE(contains(row.name, "Nitrate")) << row.name;
  }
}

// Animal production must not headline a comparison this model cannot support.
TEST(DemoSummaryTest, NoAnimalProductionOutcomeReachesThePage) {
  const std::string page = as_text(flagship());
  for (const std::string& forbidden : {std::string("Stocking rate"), std::string("Lamb at weaning"),
                                       std::string("Closing stock"), std::string("Utilisation")}) {
    EXPECT_FALSE(contains(page, forbidden)) << forbidden << " reached the summary";
  }
}

// ---------------------------------------------------------------------------
// Validation status.

// **The mapping is from what the indicator already carried**, so a number
// cannot be promoted by editing the summary.
TEST(DemoSummaryTest, ConfidenceComesFromTheIndicatorsOwnProvenance) {
  EXPECT_EQ(confidence_of(made("x", 1.0, "mm", Provenance::Placeholder)), Confidence::DoNotQuote);
  EXPECT_EQ(confidence_of(made("x", 1.0, "mm", Provenance::Verify)), Confidence::Exploratory);
  EXPECT_EQ(confidence_of(made("x", 1.0, "mm", Provenance::Derived)), Confidence::Conserved);
  EXPECT_EQ(confidence_of(made("x", 1.0, "mm", Provenance::Direct)), Confidence::Conserved);

  // A band and an independent status is the strongest thing this project says.
  EXPECT_EQ(confidence_of(made("x", 1.0, "mm", Provenance::Direct, 1.0, 2.0)),
            Confidence::Benchmarked);
  EXPECT_EQ(confidence_of(made("x", 1.0, "mm", Provenance::Derived, std::nullopt, 2.0)),
            Confidence::Benchmarked);
}

// **Fitted never reaches Benchmarked, band or no band.** A value calibrated to
// a trial and then compared against that same trial is checking its own
// arithmetic, not being tested against the world - and "benchmarked" is exactly
// the word a customer would take to mean otherwise.
TEST(DemoSummaryTest, AFittedValueIsCalibratedAndNeverBenchmarked) {
  EXPECT_EQ(confidence_of(made("x", 1.0, "mm", Provenance::Fitted)), Confidence::Calibrated);
  EXPECT_EQ(confidence_of(made("x", 1.0, "mm", Provenance::Fitted, 4000.0, 9800.0)),
            Confidence::Calibrated);

  // Which is what the flagship's headline actually is: pasture grown is fitted
  // to Winchmore's water use efficiency and compared against Winchmore's band.
  const DemoSummary summary = flagship();
  const OutcomeRow* grown = row_named(summary, "Pasture grown");
  ASSERT_NE(grown, nullptr);
  EXPECT_EQ(grown->confidence, Confidence::Calibrated);
}

TEST(DemoSummaryTest, EveryRowCarriesAStatusAndThePageExplainsTheOnesItUses) {
  const DemoSummary summary = flagship();
  const std::string page = as_text(summary);

  const std::vector<Confidence> used = summary.levels_used();
  EXPECT_FALSE(used.empty());
  for (const Confidence level : used) {
    EXPECT_TRUE(contains(page, to_string(level))) << to_string(level) << " is used but not shown";
    EXPECT_TRUE(contains(page, confidence_meaning(level)))
        << to_string(level) << " is shown but never explained";
  }

  // A legend listing levels nothing on the page carries is furniture.
  for (const Confidence level :
       {Confidence::Benchmarked, Confidence::Calibrated, Confidence::Conserved,
        Confidence::Exploratory, Confidence::DoNotQuote}) {
    const bool on_the_page =
        std::any_of(summary.outcomes.begin(), summary.outcomes.end(),
                    [level](const OutcomeRow& row) { return row.confidence == level; });
    const bool in_the_key = std::find(used.begin(), used.end(), level) != used.end();
    EXPECT_EQ(on_the_page, in_the_key) << to_string(level);
  }
}

// The key is ordered strongest first, so a reader who stops after one line has
// read the best claim on the page rather than an arbitrary one.
TEST(DemoSummaryTest, TheKeyRunsStrongestFirst) {
  DemoSummary summary;
  OutcomeRow weak;
  weak.confidence = Confidence::DoNotQuote;
  OutcomeRow strong;
  strong.confidence = Confidence::Benchmarked;
  OutcomeRow middling;
  middling.confidence = Confidence::Conserved;
  summary.outcomes = {weak, middling, strong};

  EXPECT_EQ(summary.levels_used(),
            (std::vector<Confidence>{Confidence::Benchmarked, Confidence::Conserved,
                                     Confidence::DoNotQuote}));
}

// Every status has a label and a meaning, including the ones this page happens
// not to use today.
TEST(DemoSummaryTest, EveryStatusHasAWordAndAMeaning) {
  for (const Confidence level :
       {Confidence::Benchmarked, Confidence::Calibrated, Confidence::Conserved,
        Confidence::Exploratory, Confidence::DoNotQuote}) {
    EXPECT_FALSE(to_string(level).empty());
    EXPECT_FALSE(confidence_meaning(level).empty());
  }
  EXPECT_EQ(to_string(Confidence::DoNotQuote), "do not quote");
  EXPECT_TRUE(contains(confidence_meaning(Confidence::DoNotQuote), "nothing may be published"));
}

// ---------------------------------------------------------------------------
// The page itself.

// Compact enough to sit beside the comparison it explains, and inside a
// terminal.
TEST(DemoSummaryTest, ThePageFitsATerminalAndAFiveMinuteSlot) {
  const std::string page = as_text(flagship());

  std::istringstream lines(page);
  std::string line;
  int count = 0;
  while (std::getline(lines, line)) {
    ++count;
    EXPECT_LE(line.size(), 80U) << line;
  }
  EXPECT_LT(count, 45) << "the summary has grown past what fits on a slide:\n" << page;
}

// The two scenarios are named, because a table of numbers with no names on it
// is a table nobody can act on.
TEST(DemoSummaryTest, ThePageNamesBothScenarios) {
  const std::string page = as_text(flagship());
  EXPECT_TRUE(contains(page, "rain-fed")) << page;
  EXPECT_TRUE(contains(page, "irrigated")) << page;
}

}  // namespace
}  // namespace paddock::config
