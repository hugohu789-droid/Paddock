// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#include <gtest/gtest.h>

#include <stdexcept>

#include <paddock/core/BudgetLedger.hpp>
#include <paddock/core/SimulationClock.hpp>
#include <paddock/core/SoilWater.hpp>
#include <paddock/core/Solar.hpp>
#include <paddock/core/Weather.hpp>

namespace paddock::core {
namespace {

constexpr double kCanterburyLatitude = -43.5;

// A test fixture. The shape of the numbers follows FAO-56 - a depletion
// fraction of 0.6 and a crop coefficient near 1 are what Table 22 and Table 12
// give for grazed pasture - but a real soil's available water comes from S-map
// and is carried in a TOML definition, not here.
SoilWaterParameters test_parameters() {
  SoilWaterParameters parameters;
  parameters.total_available_water_mm = 120.0;
  parameters.depletion_fraction = 0.6;
  parameters.crop_coefficient = 0.95;
  parameters.runoff_fraction = 0.05;
  return parameters;
}

DailyWeather day_of(int month, int day, double rainfall_mm, double min_c, double max_c) {
  DailyWeather weather;
  weather.date = Date{2023, month, day};
  weather.rainfall_mm = rainfall_mm;
  weather.min_air_temperature_c = min_c;
  weather.max_air_temperature_c = max_c;
  return weather;
}

TEST(EvapotranspirationTest, HargreavesGrowsWithTemperatureAndRange) {
  const double radiation = extraterrestrial_radiation_mj(kCanterburyLatitude, 15);
  const double warm = hargreaves_reference_et_mm(12.0, 26.0, radiation);
  const double cool = hargreaves_reference_et_mm(4.0, 12.0, radiation);

  EXPECT_GT(warm, cool);
  EXPECT_GT(warm, 0.0);
  // A hot Canterbury January day sits in the mid single digits of mm per day;
  // an implementation that returned 0.5 or 50 would be wrong by an order of
  // magnitude and this is the cheapest place to notice.
  EXPECT_GT(warm, 2.0);
  EXPECT_LT(warm, 12.0);
}

TEST(EvapotranspirationTest, DegenerateInputsGiveNoEvaporation) {
  EXPECT_DOUBLE_EQ(hargreaves_reference_et_mm(10.0, 10.0, 30.0), 0.0);
  EXPECT_DOUBLE_EQ(hargreaves_reference_et_mm(12.0, 8.0, 30.0), 0.0);
  EXPECT_DOUBLE_EQ(hargreaves_reference_et_mm(5.0, 15.0, 0.0), 0.0);
  // Below -17.8 C the formula turns negative; water does not come back.
  EXPECT_DOUBLE_EQ(hargreaves_reference_et_mm(-30.0, -25.0, 20.0), 0.0);
}

TEST(EvapotranspirationTest, ReferenceEtIsHigherInSummerThanInWinter) {
  const double january = reference_et_mm(day_of(1, 15, 0.0, 11.0, 24.0), kCanterburyLatitude);
  const double july = reference_et_mm(day_of(7, 15, 0.0, 1.0, 10.0), kCanterburyLatitude);

  EXPECT_GT(january, july * 3.0);
}

// FAO-56 Eq. 84: no stress until the readily available water is gone, then a
// linear fall to zero at wilting point.
TEST(WaterStressTest, FollowsTheFao56Curve) {
  constexpr double kTaw = 100.0;
  constexpr double kFraction = 0.6;

  EXPECT_DOUBLE_EQ(water_stress_coefficient(0.0, kTaw, kFraction), 1.0);
  EXPECT_DOUBLE_EQ(water_stress_coefficient(60.0, kTaw, kFraction), 1.0);
  EXPECT_DOUBLE_EQ(water_stress_coefficient(80.0, kTaw, kFraction), 0.5);
  EXPECT_DOUBLE_EQ(water_stress_coefficient(100.0, kTaw, kFraction), 0.0);
  EXPECT_DOUBLE_EQ(water_stress_coefficient(120.0, kTaw, kFraction), 0.0);
  EXPECT_DOUBLE_EQ(water_stress_coefficient(10.0, 0.0, kFraction), 0.0);
}

TEST(WaterStressTest, TheDepletionFractionMovesWithDemand) {
  // FAO-56 Table 22 note: p = p_table + 0.04 (5 - ETc), clamped to [0.1, 0.8].
  EXPECT_DOUBLE_EQ(adjusted_depletion_fraction(0.6, 5.0), 0.6);
  EXPECT_NEAR(adjusted_depletion_fraction(0.6, 8.0), 0.48, 1e-12);
  EXPECT_NEAR(adjusted_depletion_fraction(0.6, 2.0), 0.72, 1e-12);
  EXPECT_DOUBLE_EQ(adjusted_depletion_fraction(0.6, 100.0), 0.1);
  EXPECT_DOUBLE_EQ(adjusted_depletion_fraction(0.8, 0.0), 0.8);
}

TEST(SoilWaterParametersTest, TotalAvailableWaterFollowsFao56Equation82) {
  // 1000 (0.38 - 0.18) 0.6 m = 120 mm.
  EXPECT_DOUBLE_EQ(SoilWaterParameters::total_available_water(0.38, 0.18, 0.6), 120.0);
}

TEST(SoilWaterParametersTest, InvalidParametersAreRejectedByName) {
  SoilWaterParameters parameters = test_parameters();
  parameters.crop_coefficient = 0.0;
  EXPECT_NE(parameters.validation_error().find("crop_coefficient"), std::string::npos);

  parameters = test_parameters();
  parameters.depletion_fraction = 1.5;
  EXPECT_NE(parameters.validation_error().find("depletion_fraction"), std::string::npos);

  parameters = test_parameters();
  parameters.total_available_water_mm = 0.0;
  EXPECT_THROW(SoilWaterBucket(parameters, 50.0), std::invalid_argument);
}

TEST(SoilWaterBucketTest, StartsClampedIntoTheProfile) {
  EXPECT_DOUBLE_EQ(SoilWaterBucket(test_parameters(), 500.0).water_mm(), 120.0);
  EXPECT_DOUBLE_EQ(SoilWaterBucket(test_parameters(), -20.0).water_mm(), 0.0);
  EXPECT_DOUBLE_EQ(SoilWaterBucket(test_parameters(), 60.0).depletion_mm(), 60.0);
}

TEST(SoilWaterBucketTest, RainfallInfiltratesLessTheRunoffShare) {
  SoilWaterBucket bucket(test_parameters(), 20.0);

  // A cold wet winter day: almost no evaporative demand, so what infiltrates
  // stays put and the arithmetic is legible.
  const SoilWaterFluxes fluxes = bucket.step(day_of(7, 1, 20.0, 4.0, 8.0), kCanterburyLatitude);

  EXPECT_DOUBLE_EQ(fluxes.rainfall_mm, 20.0);
  EXPECT_DOUBLE_EQ(fluxes.runoff_mm, 1.0);
  EXPECT_DOUBLE_EQ(fluxes.infiltration_mm, 19.0);
  EXPECT_DOUBLE_EQ(fluxes.drainage_mm, 0.0);
  EXPECT_NEAR(bucket.water_mm(), 20.0 + 19.0 - fluxes.evapotranspiration_mm, 1e-12);
}

TEST(SoilWaterBucketTest, AFullProfileDrainsTheExcess) {
  SoilWaterBucket bucket(test_parameters(), 115.0);

  const SoilWaterFluxes fluxes = bucket.step(day_of(7, 1, 60.0, 4.0, 8.0), kCanterburyLatitude);

  EXPECT_GT(fluxes.drainage_mm, 0.0);
  EXPECT_DOUBLE_EQ(bucket.water_mm(), 120.0);
  EXPECT_NEAR(115.0 + fluxes.infiltration_mm - fluxes.evapotranspiration_mm - fluxes.drainage_mm,
              120.0, 1e-12);
}

TEST(SoilWaterBucketTest, ADrySummerEmptiesTheProfileAndStressBites) {
  SoilWaterBucket bucket(test_parameters(), 120.0);
  double first_day_et = 0.0;
  double last_day_et = 0.0;

  for (int day = 1; day <= 31; ++day) {
    const SoilWaterFluxes fluxes =
        bucket.step(day_of(1, day, 0.0, 12.0, 26.0), kCanterburyLatitude);
    if (day == 1) {
      first_day_et = fluxes.evapotranspiration_mm;
    }
    last_day_et = fluxes.evapotranspiration_mm;
  }

  EXPECT_LT(bucket.water_mm(), 30.0);
  EXPECT_LT(bucket.stress_coefficient(), 1.0);
  // Stressed pasture transpires less, which is the feedback that turns a dry
  // month into a feed deficit rather than an unlimited water loss.
  EXPECT_LT(last_day_et, first_day_et);
}

// Drying is asymptotic, not a cliff: as the profile empties the stress
// coefficient falls towards zero and takes the transpiration with it, so the
// soil approaches wilting point without ever crossing it. That is the point of
// Ks being a multiplier rather than a switch, and it is why the assertion here
// is "arbitrarily close" rather than "equal".
TEST(SoilWaterBucketTest, TheProfileApproachesButNeverPassesWiltingPoint) {
  SoilWaterBucket bucket(test_parameters(), 5.0);

  for (int day = 1; day <= 60; ++day) {
    const SoilWaterFluxes fluxes =
        bucket.step(day_of(1, (day % 31) + 1, 0.0, 14.0, 30.0), kCanterburyLatitude);
    ASSERT_GE(bucket.water_mm(), 0.0);
    ASSERT_GE(fluxes.evapotranspiration_mm, 0.0);
    ASSERT_LE(fluxes.stress_coefficient, 1.0);
  }

  EXPECT_LT(bucket.water_mm(), 0.05);
  EXPECT_LT(bucket.stress_coefficient(), 0.001);
  EXPECT_GT(bucket.stress_coefficient(), 0.0);
}

TEST(SoilWaterBucketTest, EveryFlowIsReportedToTheWaterBudget) {
  BudgetLedger ledger;
  SoilWaterBucket bucket(test_parameters(), 100.0);
  ledger.set_opening_stock(Budget::Water, bucket.water_mm());

  bucket.step(day_of(7, 1, 40.0, 3.0, 9.0), kCanterburyLatitude, &ledger);

  EXPECT_TRUE(ledger.closes(Budget::Water, bucket.water_mm()))
      << ledger.report(Budget::Water, bucket.water_mm());
  EXPECT_DOUBLE_EQ(ledger.total_inflow(Budget::Water), 40.0);
  EXPECT_EQ(ledger.entries(Budget::Water).size(), 4U);
}

}  // namespace
}  // namespace paddock::core
