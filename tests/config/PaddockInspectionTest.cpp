// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

// What the paddock inspector says about one paddock on one day.
//
// The inspector's whole job is to report what the run recorded, so what is
// tested here is that it reports it and does not improve on it: a figure the
// run did not keep comes back empty rather than as a zero, and a decision the
// model never made is not described as though it had been.

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

#include <paddock/config/PaddockInspection.hpp>
#include <paddock/core/Farmer.hpp>
#include <paddock/core/Geometry.hpp>
#include <paddock/core/PaddockMask.hpp>
#include <paddock/core/Raster.hpp>

namespace paddock::config {
namespace {

/// Two paddocks side by side over a 4 x 2 grid of ten-metre cells: the west
/// half is paddock 0, the east half paddock 1.
///
/// The origin is the north-west corner and northing counts down from it, so a
/// grid whose ground runs 0 to 20 m north starts at 20 - put it at zero and
/// every cell centre lands south of the farm, which is a mask that owns
/// nothing.
core::Raster<double> shape() {
  core::GeoTransform transform;
  transform.origin_easting = 0.0;
  transform.origin_northing = 20.0;
  transform.cell_size = 10.0;
  return {4, 2, transform, 0.0};
}

std::vector<core::Paddock> two_paddocks() {
  core::Paddock west;
  west.name = "West";
  west.boundary = core::Polygon({{0.0, 0.0}, {20.0, 0.0}, {20.0, 20.0}, {0.0, 20.0}});
  core::Paddock east;
  east.boundary = core::Polygon({{20.0, 0.0}, {40.0, 0.0}, {40.0, 20.0}, {20.0, 20.0}});
  return {west, east};
}

core::Raster<double> filled(double value) {
  core::Raster<double> raster = shape();
  for (std::size_t row = 0; row < raster.rows(); ++row) {
    for (std::size_t col = 0; col < raster.cols(); ++col) {
      raster(col, row) = value;
    }
  }
  return raster;
}

core::ManagementPolicy policy() {
  core::ManagementPolicy chosen;
  chosen.maximum_graze_days = 3;
  chosen.minimum_spell_days = 35;
  return chosen;
}

/// Water below half of what the soil holds, refill to eighty-five per cent -
/// which the policy states as depletion, the other way up.
core::IrrigationPolicy watering() {
  core::IrrigationPolicy chosen;
  chosen.enabled = true;
  chosen.trigger_depletion_fraction = 0.5;
  chosen.target_depletion_fraction = 0.15;
  return chosen;
}

bool contains(const std::string& text, const std::string& needle) {
  return text.find(needle) != std::string::npos;
}

}  // namespace

// The figures come back as the day recorded them.
TEST(PaddockInspectionTest, ItReportsTheDayItWasGiven) {
  const core::PaddockMask mask(shape(), two_paddocks());
  const core::Raster<double> cover = filled(3000.0);
  const core::Raster<double> growth = filled(45.0);

  PaddockDay day;
  day.cover = &cover;
  day.growth = &growth;
  const PaddockDayRecord record;

  const PaddockInspection inspection = inspect_paddock(0, "West", mask, day, record);

  ASSERT_TRUE(inspection.cover_kg_dm_per_ha.has_value());
  EXPECT_DOUBLE_EQ(inspection.cover_kg_dm_per_ha.value_or(0.0), 3000.0);
  ASSERT_TRUE(inspection.growth_kg_dm_per_ha.has_value());
  EXPECT_DOUBLE_EQ(inspection.growth_kg_dm_per_ha.value_or(0.0), 45.0);
  EXPECT_EQ(inspection.name, "West");
  EXPECT_EQ(inspection.cells, 4U);
}

// A paddock the survey did not name gets a label, not an invented name.
TEST(PaddockInspectionTest, AnUnnamedPaddockIsLabelledByItsNumber) {
  const core::PaddockMask mask(shape(), two_paddocks());
  const PaddockInspection inspection =
      inspect_paddock(1, "", mask, PaddockDay{}, PaddockDayRecord{});
  EXPECT_EQ(inspection.name, "Paddock 2");
}

// **A series the run did not keep is empty, not zero.** A zero would say the
// grass stopped growing, which is a claim about the farm rather than about the
// run.
TEST(PaddockInspectionTest, AMissingSeriesIsEmptyRatherThanZero) {
  const core::PaddockMask mask(shape(), two_paddocks());
  const PaddockInspection inspection =
      inspect_paddock(0, "West", mask, PaddockDay{}, PaddockDayRecord{});

  EXPECT_FALSE(inspection.cover_kg_dm_per_ha.has_value());
  EXPECT_FALSE(inspection.growth_kg_dm_per_ha.has_value());
  EXPECT_FALSE(inspection.irrigation_today_mm.has_value());
  EXPECT_FALSE(inspection.rest_days.has_value());
}

// Each paddock is averaged over its own cells and not over the farm.
TEST(PaddockInspectionTest, EachPaddockIsAveragedOverItsOwnCells) {
  const core::PaddockMask mask(shape(), two_paddocks());
  core::Raster<double> cover = filled(0.0);
  for (std::size_t row = 0; row < cover.rows(); ++row) {
    for (std::size_t col = 0; col < cover.cols(); ++col) {
      cover(col, row) = col < 2 ? 1000.0 : 4000.0;
    }
  }

  PaddockDay day;
  day.cover = &cover;

  const PaddockInspection west = inspect_paddock(0, "", mask, day, PaddockDayRecord{});
  const PaddockInspection east = inspect_paddock(1, "", mask, day, PaddockDayRecord{});

  ASSERT_TRUE(west.cover_kg_dm_per_ha.has_value());
  ASSERT_TRUE(east.cover_kg_dm_per_ha.has_value());
  EXPECT_DOUBLE_EQ(west.cover_kg_dm_per_ha.value_or(0.0), 1000.0);
  EXPECT_DOUBLE_EQ(east.cover_kg_dm_per_ha.value_or(0.0), 4000.0);
}

// Irrigation off in the scenario is said plainly, and nothing else is claimed.
TEST(PaddockInspectionTest, IrrigationOffIsSaidPlainly) {
  const core::PaddockMask mask(shape(), two_paddocks());
  const core::IrrigationPolicy off;
  PaddockDayRecord record;
  record.irrigation = &off;

  const PaddockInspection inspection = inspect_paddock(0, "", mask, PaddockDay{}, record);

  EXPECT_FALSE(inspection.irrigation_enabled);
  // Nothing for a phrase to add: the panel's own status row says it is off, and
  // a second sentence saying the same thing is noise.
  EXPECT_TRUE(irrigation_reason_phrase(inspection).empty());
}

// **A watered day is explained from the morning it was decided on.** The soil
// was at 48% when the schedule looked, below the 50% trigger, and it refills to
// 85% - which is the whole of the rule and every number in it is recorded.
TEST(PaddockInspectionTest, AWateredDayIsExplainedFromThatMorning) {
  const core::PaddockMask mask(shape(), two_paddocks());
  const core::Raster<double> water = filled(20.1);
  const core::Raster<double> morning = filled(0.48);
  const core::IrrigationPolicy rules = watering();

  PaddockDay day;
  day.irrigation_today = &water;
  day.morning_water = &morning;
  PaddockDayRecord record;
  record.irrigation = &rules;

  const PaddockInspection inspection = inspect_paddock(0, "", mask, day, record);

  ASSERT_TRUE(inspection.irrigation_today_mm.has_value());
  EXPECT_NEAR(inspection.irrigation_today_mm.value_or(0.0), 20.1, 1e-9);
  EXPECT_NEAR(inspection.irrigation_trigger_fraction, 0.5, 1e-9);
  EXPECT_NEAR(inspection.irrigation_target_fraction, 0.85, 1e-9);

  // The numbers the decision was made from travel as figures, so the panel can
  // put them in a column and a reader can check the rule for themselves.
  ASSERT_TRUE(inspection.morning_water_fraction.has_value());
  EXPECT_NEAR(inspection.morning_water_fraction.value_or(0.0), 0.48, 1e-9);

  EXPECT_EQ(irrigation_reason_phrase(inspection), "at or below the trigger")
      << "the step between what the soil was and what went on it";
}

// **The reason for a dry day is the schedule's own, not one worked out here.**
TEST(PaddockInspectionTest, ADryDayGivesTheSchedulesOwnReason) {
  const core::PaddockMask mask(shape(), two_paddocks());
  const core::Raster<double> morning = filled(0.64);
  const core::IrrigationPolicy rules = watering();

  PaddockDay day;
  day.morning_water = &morning;
  PaddockDayRecord record;
  record.irrigation = &rules;
  record.held_back = "the profile is still wetter than the trigger";

  const PaddockInspection inspection = inspect_paddock(0, "", mask, day, record);

  EXPECT_EQ(irrigation_reason_phrase(inspection), "the profile is still wetter than the trigger")
      << "the schedule's own words, not a reason worked out here";
  ASSERT_TRUE(inspection.morning_water_fraction.has_value());
  EXPECT_NEAR(inspection.morning_water_fraction.value_or(0.0), 0.64, 1e-9);
  EXPECT_NEAR(inspection.irrigation_trigger_fraction, 0.5, 1e-9);
}

// **With no morning recorded, nothing is claimed about the decision.** The
// water left at the end of the day is not what the schedule read, and a run
// that kept only that gets a sentence with no explanation in it rather than one
// that explains the day backwards.
TEST(PaddockInspectionTest, WithoutTheMorningNoReasonIsGiven) {
  const core::PaddockMask mask(shape(), two_paddocks());
  const core::Raster<double> evening = filled(0.64);
  const core::IrrigationPolicy rules = watering();

  PaddockDay day;
  day.available_water = &evening;
  PaddockDayRecord record;
  record.irrigation = &rules;

  const PaddockInspection inspection = inspect_paddock(0, "", mask, day, record);

  EXPECT_TRUE(irrigation_reason_phrase(inspection).empty())
      << "a reason was offered for a decision nothing was recorded about";
  EXPECT_FALSE(inspection.morning_water_fraction.has_value())
      << "the evening figure was passed off as the morning the schedule read";
}

// The rest a paddock has had comes from the farm's own count.
TEST(PaddockInspectionTest, RestComesFromTheFarmsOwnCount) {
  const core::PaddockMask mask(shape(), two_paddocks());
  const std::vector<int> rest{12, 40};
  const std::vector<std::size_t> grazed{1};
  const core::ManagementPolicy rules = policy();

  PaddockDayRecord record;
  record.rest_days = &rest;
  record.grazed = &grazed;
  record.policy = &rules;

  const PaddockInspection west = inspect_paddock(0, "", mask, PaddockDay{}, record);
  const PaddockInspection east = inspect_paddock(1, "", mask, PaddockDay{}, record);

  ASSERT_TRUE(west.rest_days.has_value());
  EXPECT_EQ(west.rest_days.value_or(0.0), 12);
  EXPECT_EQ(west.minimum_spell_days, 35);
  EXPECT_FALSE(west.stock_today);
  EXPECT_TRUE(east.stock_today) << "the farm listed this paddock as carrying stock";
}

// **The rule is stated, and no verdict is passed.** This farm has no
// per-paddock grazeable test: a mob moves on hunger or on days grazed, and goes
// to the free paddock that has rested longest. An inspector that said "not
// grazeable: rested 12 of 35 days" would be describing a rule the model does
// not have.
TEST(PaddockInspectionTest, TheGrazingSentenceStatesTheRuleAndRefusesAVerdict) {
  const std::string sentence = grazing_rule_sentence(policy());

  EXPECT_TRUE(contains(sentence, "rested longest"));
  EXPECT_TRUE(contains(sentence, "not a gate"));
  EXPECT_FALSE(contains(sentence, "grazeable"))
      << "the inspector passed a verdict the model never reaches";
}

// ---------------------------------------------------------------------------
// The decision status.
//
// "0.0 mm applied" is three different days, and a person watching a
// demonstration cannot tell them apart from the figure. These check the status
// names which day it was, off what the run recorded and nothing else.

TEST(PaddockInspectionTest, ADayWithNoIrrigationRuleReadsAsOff) {
  const core::PaddockMask mask(shape(), two_paddocks());
  const PaddockDay day;
  const PaddockDayRecord record;

  const PaddockInspection inspection = inspect_paddock(0, "", mask, day, record);
  EXPECT_EQ(inspection.irrigation_status(), PaddockInspection::IrrigationStatus::Off);
  EXPECT_EQ(inspection.irrigation_status_text(), "off in this scenario");
}

TEST(PaddockInspectionTest, WaterOnTheGroundReadsAsWatered) {
  const core::PaddockMask mask(shape(), two_paddocks());
  const core::Raster<double> water = filled(20.1);
  const core::IrrigationPolicy rules = watering();

  PaddockDay day;
  day.irrigation_today = &water;
  PaddockDayRecord record;
  record.irrigation = &rules;

  const PaddockInspection inspection = inspect_paddock(0, "", mask, day, record);
  EXPECT_EQ(inspection.irrigation_status(), PaddockInspection::IrrigationStatus::Watered);
  EXPECT_EQ(inspection.irrigation_status_text(), "watered");
}

// **Water that went on outranks a reason recorded for somewhere else.** The
// schedule keeps one held-back sentence for the whole farm - the first cell
// that hit a limit speaks for the rest - so a paddock that got water and a farm
// that recorded a reason is still a paddock that got water.
TEST(PaddockInspectionTest, APaddockThatGotWaterIsWateredEvenWithAReasonRecorded) {
  const core::PaddockMask mask(shape(), two_paddocks());
  const core::Raster<double> water = filled(12.0);
  const core::IrrigationPolicy rules = watering();

  PaddockDay day;
  day.irrigation_today = &water;
  PaddockDayRecord record;
  record.irrigation = &rules;
  record.held_back = "watered too recently";

  const PaddockInspection inspection = inspect_paddock(0, "", mask, day, record);
  EXPECT_EQ(inspection.irrigation_status(), PaddockInspection::IrrigationStatus::Watered);
}

TEST(PaddockInspectionTest, ADryDayWithAReasonReadsAsHeldBack) {
  const core::PaddockMask mask(shape(), two_paddocks());
  const core::IrrigationPolicy rules = watering();

  const PaddockDay day;
  PaddockDayRecord record;
  record.irrigation = &rules;
  record.held_back = "watered too recently";

  const PaddockInspection inspection = inspect_paddock(0, "", mask, day, record);
  EXPECT_EQ(inspection.irrigation_status(), PaddockInspection::IrrigationStatus::HeldBack);
  EXPECT_EQ(inspection.irrigation_status_text(), "held back");
  EXPECT_EQ(irrigation_reason_phrase(inspection), "watered too recently");
}

// Irrigation on, nothing applied, no reason recorded. Said as "not recorded"
// rather than guessed at, which is what a run that kept no irrigation series
// honestly amounts to.
TEST(PaddockInspectionTest, NothingRecordedIsSaidRatherThanGuessedAt) {
  const core::PaddockMask mask(shape(), two_paddocks());
  const core::IrrigationPolicy rules = watering();

  const PaddockDay day;
  PaddockDayRecord record;
  record.irrigation = &rules;

  const PaddockInspection inspection = inspect_paddock(0, "", mask, day, record);
  EXPECT_EQ(inspection.irrigation_status(), PaddockInspection::IrrigationStatus::NotRecorded);
  EXPECT_EQ(inspection.irrigation_status_text(), "not recorded");
}

// ---------------------------------------------------------------------------
// The panel.

namespace {

const PanelSection* section_named(const InspectorPanel& panel, const std::string& title) {
  for (const PanelSection& group : panel.sections) {
    if (group.title == title) {
      return &group;
    }
  }
  return nullptr;
}

const PanelRow* row_named(const PanelSection& group, const std::string& label) {
  for (const PanelRow& line : group.rows) {
    if (line.label == label) {
      return &line;
    }
  }
  return nullptr;
}

/// A watered day: 38% when the schedule looked this morning, 84% by tonight.
/// The exact shape a reader misreads if the panel does not separate the two.
PaddockInspection watered_day() {
  const core::PaddockMask mask(shape(), two_paddocks());
  static const core::Raster<double> applied = filled(25.0);
  static const core::Raster<double> morning = filled(0.38);
  static const core::Raster<double> tonight = filled(0.84);
  static const core::Raster<double> to_date = filled(275.0);
  static const core::IrrigationPolicy rules = watering();

  PaddockDay day;
  day.irrigation_today = &applied;
  day.morning_water = &morning;
  day.available_water = &tonight;
  day.irrigation_to_date = &to_date;
  PaddockDayRecord record;
  record.irrigation = &rules;
  return inspect_paddock(0, "", mask, day, record);
}

}  // namespace

// **The question this panel exists to stop somebody asking.** Tonight's 84% and
// this morning's 38% are the same soil at two ends of a day, and they have to
// be in different sections, each saying which end it is.
TEST(PaddockInspectorPanelTest, TonightAndThisMorningAreNeverTheSameSection) {
  const InspectorPanel panel = inspector_panel(watered_day(), "the rule");

  const PanelSection* tonight = section_named(panel, "Water tonight");
  ASSERT_NE(tonight, nullptr);
  EXPECT_FALSE(tonight->subtitle.empty()) << "nothing says which end of the day this is";
  EXPECT_TRUE(contains(tonight->subtitle, "irrigation"))
      << "tonight's water has to say the irrigation is already in it: " << tonight->subtitle;
  const PanelRow* now = row_named(*tonight, "Available water");
  ASSERT_NE(now, nullptr);
  EXPECT_EQ(now->value, "84%");

  const PanelSection* irrigation = section_named(panel, "Irrigation");
  ASSERT_NE(irrigation, nullptr);
  EXPECT_TRUE(contains(irrigation->subtitle, "morning")) << irrigation->subtitle;
  const PanelRow* then = row_named(*irrigation, "Soil water then");
  ASSERT_NE(then, nullptr);
  EXPECT_EQ(then->value, "38%");
  EXPECT_FALSE(then->note.empty()) << "the morning figure has to say what it was for";

  // And the trigger it was tested against, so the arithmetic is on the page.
  const PanelRow* trigger = row_named(*irrigation, "Waters at or below");
  ASSERT_NE(trigger, nullptr);
  EXPECT_EQ(trigger->value, "50%");
}

// The explanation sits inside the irrigation section, between the reading and
// the water. It used to be appended after the whole table, which put it under
// Grazing - the one place a reader looking at irrigation would not find it.
TEST(PaddockInspectorPanelTest, TheReasonSitsBetweenTheReadingAndTheWater) {
  const InspectorPanel panel = inspector_panel(watered_day(), "the rule");
  const PanelSection* irrigation = section_named(panel, "Irrigation");
  ASSERT_NE(irrigation, nullptr);

  std::vector<std::string> labels;
  labels.reserve(irrigation->rows.size());
  for (const PanelRow& line : irrigation->rows) {
    labels.push_back(line.label);
  }
  const auto at = [&labels](const std::string& label) {
    return std::find(labels.begin(), labels.end(), label) - labels.begin();
  };

  ASSERT_NE(row_named(*irrigation, "Because"), nullptr) << "no explanation was offered";
  EXPECT_LT(at("Soil water then"), at("Because"));
  EXPECT_LT(at("Because"), at("Applied today"));
  EXPECT_EQ(irrigation->rows.front().label, "Decision") << "the status leads the section";
}

// The growth factor says which end of its own scale is good. Without that,
// "1.00" reads as the worst day of the year as readily as the best.
TEST(PaddockInspectorPanelTest, TheGrowthFactorCarriesItsSemantics) {
  const InspectorPanel panel = inspector_panel(watered_day(), "the rule");
  const PanelSection* tonight = section_named(panel, "Water tonight");
  ASSERT_NE(tonight, nullptr);
  const PanelRow* factor = row_named(*tonight, "Growth factor");
  ASSERT_NE(factor, nullptr);
  EXPECT_TRUE(contains(factor->note, "1.00")) << factor->note;
  EXPECT_TRUE(contains(factor->note, "unrestricted")) << factor->note;
}

// **The refill target is where the water was aiming**, so it belongs on a day
// water went on and nowhere else. On a dry day it is a setting, and this panel
// has no line to spend on settings.
TEST(PaddockInspectorPanelTest, TheRefillTargetShowsOnlyWhereItExplainsSomething) {
  const core::PaddockMask mask(shape(), two_paddocks());
  const core::Raster<double> morning = filled(0.64);
  const core::IrrigationPolicy rules = watering();
  PaddockDay dry;
  dry.morning_water = &morning;
  PaddockDayRecord record;
  record.irrigation = &rules;
  record.held_back = "the profile is still wetter than the trigger";

  const InspectorPanel watered = inspector_panel(watered_day(), "the rule");
  const InspectorPanel held =
      inspector_panel(inspect_paddock(0, "", mask, dry, record), "the rule");

  ASSERT_NE(section_named(watered, "Irrigation"), nullptr);
  ASSERT_NE(section_named(held, "Irrigation"), nullptr);
  EXPECT_NE(row_named(*section_named(watered, "Irrigation"), "Refilling toward"), nullptr);
  EXPECT_EQ(row_named(*section_named(held, "Irrigation"), "Refilling toward"), nullptr);

  // But the morning reading shows on both. A dry day is exactly when somebody
  // asks why nothing happened, and the answer is a reading and a trigger.
  const PanelRow* then = row_named(*section_named(held, "Irrigation"), "Soil water then");
  ASSERT_NE(then, nullptr);
  EXPECT_EQ(then->value, "64%");
  EXPECT_NE(row_named(*section_named(held, "Irrigation"), "Waters at or below"), nullptr);
}

// A rain-fed scenario says so in one line and spends nothing else on it.
TEST(PaddockInspectorPanelTest, RainFedIsOneLine) {
  const core::PaddockMask mask(shape(), two_paddocks());
  const PaddockDay day;
  const PaddockDayRecord record;
  const InspectorPanel panel =
      inspector_panel(inspect_paddock(0, "", mask, day, record), "the rule");

  const PanelSection* irrigation = section_named(panel, "Irrigation");
  ASSERT_NE(irrigation, nullptr);
  ASSERT_EQ(irrigation->rows.size(), 1U);
  EXPECT_EQ(irrigation->rows.front().label, "Decision");
  EXPECT_EQ(irrigation->rows.front().value, "off in this scenario");
  EXPECT_TRUE(irrigation->subtitle.empty());
}

// **No gate the model does not have.** A mob moves when it has gone short or
// has been somewhere long enough, and goes to whichever free paddock has rested
// longest - there is no per-paddock eligibility test anywhere in the model, so
// the panel must not imply one. The spell is labelled as a target and says in
// as many words that a mob moves onto ground that has had less.
TEST(PaddockInspectorPanelTest, GrazingClaimsNoGateTheModelDoesNotHave) {
  const core::PaddockMask mask(shape(), two_paddocks());
  const std::vector<int> rest{12, 40};
  const std::vector<std::size_t> grazed{0};
  const core::ManagementPolicy rules = policy();

  const PaddockDay day;
  PaddockDayRecord record;
  record.rest_days = &rest;
  record.grazed = &grazed;
  record.policy = &rules;

  const InspectorPanel panel =
      inspector_panel(inspect_paddock(0, "", mask, day, record), grazing_rule_sentence(rules));

  const PanelSection* grazing = section_named(panel, "Grazing");
  ASSERT_NE(grazing, nullptr);
  EXPECT_EQ(row_named(*grazing, "Stock today")->value, "on it");
  EXPECT_EQ(row_named(*grazing, "Rested")->value, "12 days");

  const PanelRow* spell = row_named(*grazing, "Farmer aims at");
  ASSERT_NE(spell, nullptr) << "the spell must not be labelled a minimum";
  EXPECT_TRUE(contains(spell->note, "not a gate")) << spell->note;

  // Nothing in the panel offers a verdict on whether this paddock could have
  // been grazed, because nothing in the model decides that.
  for (const PanelRow& line : grazing->rows) {
    EXPECT_FALSE(contains(line.label, "Grazeable")) << line.label;
    EXPECT_FALSE(contains(line.value, "eligible")) << line.value;
    EXPECT_FALSE(contains(line.value, "ready")) << line.value;
  }
}

// The header carries which piece of ground this is, and nothing that changes
// from day to day.
TEST(PaddockInspectorPanelTest, TheHeaderNamesTheGroundAndItsSize) {
  const core::PaddockMask mask(shape(), two_paddocks());
  const PaddockDay day;
  const PaddockDayRecord record;
  const InspectorPanel panel =
      inspector_panel(inspect_paddock(0, "West", mask, day, record), "the rule");

  EXPECT_EQ(panel.heading, "West");
  EXPECT_TRUE(contains(panel.subheading, "ha")) << panel.subheading;
  EXPECT_TRUE(contains(panel.subheading, "cells")) << panel.subheading;
  EXPECT_EQ(panel.grazing_rule, "the rule");
}

// ---------------------------------------------------------------------------
// Selection.

// **Scrubbing the timeline keeps the paddock.** The same fences all year is the
// whole reason a selection is worth holding, and it is what makes walking one
// paddock through a season possible.
TEST(PaddockSelectionTest, TheSameFencesKeepTheSelection) {
  const std::vector<core::Paddock> paddocks = two_paddocks();
  EXPECT_TRUE(selection_survives(paddocks, paddocks, 0));
  EXPECT_TRUE(selection_survives(paddocks, paddocks, 1));
}

// A re-run of the same farm keeps it too - which is what the flagship demo
// needs: click a paddock, run rain-fed, run irrigated, read the same ground.
TEST(PaddockSelectionTest, ARerunOfTheSameFarmKeepsTheSelection) {
  const std::vector<core::Paddock> before = two_paddocks();
  const std::vector<core::Paddock> after = two_paddocks();
  EXPECT_TRUE(selection_survives(before, after, 0));
}

// **A different farm drops it rather than pointing at ground nobody clicked.**
// An index that is still in range is not the same field.
TEST(PaddockSelectionTest, ADifferentFarmDropsTheSelection) {
  const std::vector<core::Paddock> before = two_paddocks();

  std::vector<core::Paddock> renamed = two_paddocks();
  renamed[0].name = "Somewhere else";
  EXPECT_FALSE(selection_survives(before, renamed, 0));

  std::vector<core::Paddock> resized = two_paddocks();
  resized[0].boundary = core::Polygon({{0.0, 0.0}, {10.0, 0.0}, {10.0, 20.0}, {0.0, 20.0}});
  EXPECT_FALSE(selection_survives(before, resized, 0))
      << "a paddock half the size is not the paddock that was clicked";
}

// Fewer paddocks, or none, drops it - including the index that would still be
// in range.
TEST(PaddockSelectionTest, ASmallerFarmDropsTheSelection) {
  const std::vector<core::Paddock> before = two_paddocks();
  const std::vector<core::Paddock> one{two_paddocks().front()};

  EXPECT_FALSE(selection_survives(before, one, 0))
      << "a farm with a different count is not this one";
  EXPECT_FALSE(selection_survives(before, one, 1)) << "out of range on the new farm";
  EXPECT_FALSE(selection_survives(before, {}, 0));
  EXPECT_FALSE(selection_survives({}, before, 0));
}

}  // namespace paddock::config
