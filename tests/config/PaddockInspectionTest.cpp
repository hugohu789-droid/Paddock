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
  PaddockDayRecord record;
  record.irrigation_enabled = false;

  const PaddockInspection inspection = inspect_paddock(0, "", mask, PaddockDay{}, record);

  EXPECT_FALSE(inspection.irrigation_enabled);
  EXPECT_EQ(irrigation_sentence(inspection), "Irrigation is off in this scenario.");
}

// Water on the ground today is reported as having happened.
TEST(PaddockInspectionTest, WaterPutOnTodayIsReportedAsWatered) {
  const core::PaddockMask mask(shape(), two_paddocks());
  const core::Raster<double> water = filled(20.1);

  PaddockDay day;
  day.irrigation_today = &water;
  PaddockDayRecord record;
  record.irrigation_enabled = true;

  const PaddockInspection inspection = inspect_paddock(0, "", mask, day, record);

  ASSERT_TRUE(inspection.irrigation_today_mm.has_value());
  EXPECT_NEAR(*inspection.irrigation_today_mm, 20.1, 1e-9);
  EXPECT_TRUE(contains(irrigation_sentence(inspection), "Watered today"));
}

// **A dry day is not explained by the water left at the end of it.** The
// schedule decides in the morning on the depletion as it stood then, and the
// figure kept here is the day's end - so the sentence reports the figure and
// does not say the trigger was or was not met.
TEST(PaddockInspectionTest, ADryDayReportsTheFigureRatherThanAReason) {
  const core::PaddockMask mask(shape(), two_paddocks());
  const core::Raster<double> left = filled(0.64);

  PaddockDay day;
  day.available_water = &left;
  PaddockDayRecord record;
  record.irrigation_enabled = true;

  const PaddockInspection inspection = inspect_paddock(0, "", mask, day, record);
  const std::string sentence = irrigation_sentence(inspection);

  EXPECT_TRUE(contains(sentence, "Not watered today"));
  EXPECT_TRUE(contains(sentence, "64%"));
  EXPECT_FALSE(contains(sentence, "trigger"))
      << "the sentence explained a decision from the wrong end of the day";
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
