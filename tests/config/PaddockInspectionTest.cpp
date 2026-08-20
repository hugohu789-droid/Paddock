// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

// What the paddock inspector says about one paddock on one day.
//
// The inspector's whole job is to report what the run recorded, so what is
// tested here is that it reports it and does not improve on it: a figure the
// run did not keep comes back empty rather than as a zero, and a decision the
// model never made is not described as though it had been.

#include <gtest/gtest.h>

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
  return core::Raster<double>(4, 2, transform, 0.0);
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
  PaddockDayRecord record;

  const PaddockInspection inspection = inspect_paddock(0, "West", mask, day, record);

  ASSERT_TRUE(inspection.cover_kg_dm_per_ha.has_value());
  EXPECT_DOUBLE_EQ(*inspection.cover_kg_dm_per_ha, 3000.0);
  ASSERT_TRUE(inspection.growth_kg_dm_per_ha.has_value());
  EXPECT_DOUBLE_EQ(*inspection.growth_kg_dm_per_ha, 45.0);
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
  EXPECT_DOUBLE_EQ(*west.cover_kg_dm_per_ha, 1000.0);
  EXPECT_DOUBLE_EQ(*east.cover_kg_dm_per_ha, 4000.0);
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
  EXPECT_NEAR(*inspection.irrigation_today_mm, 20.1, 1e-9);
  EXPECT_NEAR(inspection.irrigation_trigger_fraction, 0.5, 1e-9);
  EXPECT_NEAR(inspection.irrigation_target_fraction, 0.85, 1e-9);

  // The numbers the decision was made from travel as figures, so the panel can
  // put them in a column and a reader can check the rule for themselves.
  ASSERT_TRUE(inspection.morning_water_fraction.has_value());
  EXPECT_NEAR(*inspection.morning_water_fraction, 0.48, 1e-9);

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
  EXPECT_NEAR(*inspection.morning_water_fraction, 0.64, 1e-9);
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
  EXPECT_EQ(*west.rest_days, 12);
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

}  // namespace paddock::config
