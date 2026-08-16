#include <gtest/gtest.h>

#include <cmath>
#include <stdexcept>
#include <string>

#include <paddock/core/Pasture.hpp>
#include <paddock/core/SimulationClock.hpp>
#include <paddock/core/Weather.hpp>

#include "support/TestPasture.hpp"

namespace paddock::core {
namespace {

using test_support::test_sward_parameters;

DailyWeather growing_day(double radiation_mj, double min_c, double max_c) {
  DailyWeather weather;
  weather.date = Date{2023, 11, 15};
  weather.solar_radiation_mj_per_m2 = radiation_mj;
  weather.min_air_temperature_c = min_c;
  weather.max_air_temperature_c = max_c;
  return weather;
}

PastureSward test_sward(double grass_kg = 1800.0, double legume_kg = 400.0,
                        double soil_nitrogen_kg = 60.0) {
  return {test_sward_parameters(), grass_kg, legume_kg, soil_nitrogen_kg};
}

TEST(TemperatureResponseTest, IsZeroOutsideTheCardinalTemperaturesAndOneAtTheOptimum) {
  EXPECT_DOUBLE_EQ(temperature_response(4.4, 4.4, 20.0, 35.0), 0.0);
  EXPECT_DOUBLE_EQ(temperature_response(2.0, 4.4, 20.0, 35.0), 0.0);
  EXPECT_DOUBLE_EQ(temperature_response(20.0, 4.4, 20.0, 35.0), 1.0);
  EXPECT_DOUBLE_EQ(temperature_response(35.0, 4.4, 20.0, 35.0), 0.0);
  EXPECT_DOUBLE_EQ(temperature_response(40.0, 4.4, 20.0, 35.0), 0.0);
}

TEST(TemperatureResponseTest, RisesToTheOptimumAndFallsAfterIt) {
  const double cool = temperature_response(8.0, 4.4, 20.0, 35.0);
  const double warm = temperature_response(16.0, 4.4, 20.0, 35.0);
  const double hot = temperature_response(30.0, 4.4, 20.0, 35.0);

  EXPECT_GT(warm, cool);
  EXPECT_LT(hot, 1.0);
  EXPECT_GT(hot, 0.0);
  EXPECT_NEAR(temperature_response(12.2, 4.4, 20.0, 35.0), 0.5, 1e-12);
}

TEST(LightInterceptionTest, FollowsBeersLaw) {
  EXPECT_DOUBLE_EQ(light_interception(0.0, 0.5), 0.0);
  EXPECT_DOUBLE_EQ(light_interception(-1.0, 0.5), 0.0);
  EXPECT_NEAR(light_interception(1.0, 0.5), 1.0 - std::exp(-0.5), 1e-12);

  // A closed canopy takes nearly everything, and more leaf cannot take more
  // than all of it: that saturation is why cover stops paying past a point.
  EXPECT_GT(light_interception(6.0, 0.5), 0.94);
  EXPECT_LT(light_interception(12.0, 0.5), 1.0);
  EXPECT_GT(light_interception(12.0, 0.5), light_interception(6.0, 0.5));
}

TEST(PastureSwardTest, GrowsOnLightTemperatureAndWater) {
  PastureSward sward = test_sward();
  const double before = sward.green_kg_dm();

  const PastureGrowth growth = sward.step(growing_day(20.0, 10.0, 22.0), 1.0);

  EXPECT_GT(growth.total_growth_kg_dm(), 0.0);
  EXPECT_GT(growth.intercepted_par_mj_per_m2, 0.0);
  EXPECT_GT(growth.temperature_factor, 0.0);
  EXPECT_DOUBLE_EQ(growth.water_factor, 1.0);
  EXPECT_GT(sward.green_kg_dm() + sward.dead_kg_dm(), before);
}

TEST(PastureSwardTest, DarknessAndFrostStopGrowth) {
  PastureSward dark = test_sward();
  PastureSward frozen = test_sward();

  EXPECT_DOUBLE_EQ(dark.step(growing_day(0.0, 10.0, 22.0), 1.0).total_growth_kg_dm(), 0.0);
  EXPECT_DOUBLE_EQ(frozen.step(growing_day(20.0, -4.0, 2.0), 1.0).total_growth_kg_dm(), 0.0);
}

TEST(PastureSwardTest, WaterStressScalesGrowthDirectly) {
  PastureSward wet = test_sward();
  PastureSward dry = test_sward();
  PastureSward wilted = test_sward();

  const double wet_growth = wet.step(growing_day(20.0, 10.0, 22.0), 1.0).total_growth_kg_dm();
  const double dry_growth = dry.step(growing_day(20.0, 10.0, 22.0), 0.5).total_growth_kg_dm();
  const double no_growth = wilted.step(growing_day(20.0, 10.0, 22.0), 0.0).total_growth_kg_dm();

  EXPECT_NEAR(dry_growth, wet_growth * 0.5, 1e-9);
  EXPECT_DOUBLE_EQ(no_growth, 0.0);
}

// The point of modelling two species rather than "pasture": fixation is the
// only way nitrogen enters this system, and it is what a clover-rich sward
// brings that a pure grass one does not.
TEST(PastureSwardTest, TheLegumeIsTheOnlyWayNitrogenEntersTheSystem) {
  PastureSward sward = test_sward(1800.0, 400.0, 40.0);
  const double before = sward.total_nitrogen_kg();

  const PastureGrowth growth = sward.step(growing_day(20.0, 10.0, 22.0), 1.0);

  EXPECT_GT(growth.nitrogen_fixed_kg, 0.0);
  // 25 kg N per tonne of legume dry matter (fixture value; published range is
  // 20-28 kg N/t for white clover).
  EXPECT_NEAR(growth.nitrogen_fixed_kg, growth.legume_growth_kg_dm * 25.0 / 1000.0, 1e-12);
  // Everything else is a transfer between pools, so the system gained exactly
  // what the clover fixed.
  EXPECT_NEAR(sward.total_nitrogen_kg() - before, growth.nitrogen_fixed_kg, 1e-12);
}

// Fixation covers about half of what clover tissue contains, so clover draws on
// the soil too; a grass-only sward has no nitrogen income at all.
TEST(PastureSwardTest, ASwardWithoutLegumeGainsNoNitrogen) {
  PastureSward grass_only = test_sward(2200.0, 0.0, 80.0);
  const double before = grass_only.total_nitrogen_kg();

  for (int day = 0; day < 30; ++day) {
    grass_only.step(growing_day(20.0, 10.0, 22.0), 1.0);
  }

  EXPECT_NEAR(grass_only.total_nitrogen_kg(), before, 1e-9);
}

TEST(PastureSwardTest, TheLegumeIsLimitedByTheSoilNitrogenItStillNeeds) {
  PastureSward starved = test_sward(0.0, 400.0, 0.0);

  const PastureGrowth growth = starved.step(growing_day(20.0, 10.0, 22.0), 1.0);

  // Clover fixes 25 of the 45 kg N a tonne of its dry matter holds; with no
  // soil nitrogen for the remaining 20 it cannot grow.
  EXPECT_DOUBLE_EQ(growth.legume_growth_kg_dm, 0.0);
  EXPECT_DOUBLE_EQ(growth.nitrogen_fixed_kg, 0.0);
}

TEST(PastureSwardTest, TheGrassIsLimitedByTheNitrogenTheSoilCanSupply) {
  PastureSward starved = test_sward(1800.0, 0.0, 0.0);
  PastureSward fed = test_sward(1800.0, 0.0, 200.0);

  const PastureGrowth starved_growth = starved.step(growing_day(20.0, 10.0, 22.0), 1.0);
  const PastureGrowth fed_growth = fed.step(growing_day(20.0, 10.0, 22.0), 1.0);

  EXPECT_DOUBLE_EQ(starved_growth.grass_growth_kg_dm, 0.0);
  EXPECT_DOUBLE_EQ(starved_growth.nitrogen_factor, 0.0);
  EXPECT_GT(fed_growth.grass_growth_kg_dm, 0.0);
  EXPECT_DOUBLE_EQ(fed_growth.nitrogen_factor, 1.0);
}

TEST(PastureSwardTest, SoilNitrogenIsNeverOverdrawn) {
  PastureSward sward = test_sward(3000.0, 0.0, 0.5);

  for (int day = 0; day < 30; ++day) {
    sward.step(growing_day(25.0, 12.0, 24.0), 1.0);
    ASSERT_GE(sward.soil_mineral_nitrogen_kg(), 0.0);
  }
}

TEST(PastureSwardTest, SenescenceMovesGreenToDeadAndDecompositionRemovesIt) {
  PastureSward sward = test_sward();

  // No light, so nothing grows and only turnover moves dry matter.
  const PastureGrowth growth = sward.step(growing_day(0.0, 10.0, 22.0), 1.0);

  EXPECT_DOUBLE_EQ(growth.total_growth_kg_dm(), 0.0);
  EXPECT_GT(growth.senescence_kg_dm, 0.0);
  EXPECT_GT(sward.dead_kg_dm(), 0.0);
  // Only the dry matter above each species' residual senesces.
  EXPECT_NEAR(growth.senescence_kg_dm, ((1800.0 - 400.0) * 0.02) + ((400.0 - 100.0) * 0.025), 1e-9);
  EXPECT_NEAR(growth.decomposition_kg_dm, growth.senescence_kg_dm * 0.02, 1e-9);
}

// Zero cover must not be an absorbing state. A sward dried or grazed back to
// its residual has to be able to grow again when the weather turns, which is
// the difference between a model of a pasture and a model of a lawn that died.
TEST(PastureSwardTest, ASwardHeldAtItsResidualStillRegrows) {
  PastureSward sward = test_sward(420.0, 110.0, 80.0);

  // A month of darkness cannot senesce the residual away.
  for (int day = 0; day < 30; ++day) {
    sward.step(growing_day(0.0, 10.0, 22.0), 1.0);
  }
  EXPECT_GE(sward.grass_kg_dm(), 400.0);
  EXPECT_GE(sward.legume_kg_dm(), 100.0);

  const double before = sward.green_kg_dm();
  for (int day = 0; day < 30; ++day) {
    sward.step(growing_day(20.0, 10.0, 22.0), 1.0);
  }

  EXPECT_GT(sward.green_kg_dm(), before);
}

TEST(PastureSwardTest, DecompositionMineralisesNitrogenBackIntoTheSoil) {
  PastureSward sward = test_sward(1800.0, 400.0, 0.0);
  const double before = sward.soil_mineral_nitrogen_kg();

  for (int day = 0; day < 10; ++day) {
    sward.step(growing_day(0.0, 10.0, 22.0), 1.0);
  }

  EXPECT_GT(sward.soil_mineral_nitrogen_kg(), before);
}

TEST(PastureSwardTest, LeafAreaAndCoverFollowTheStandingDryMatter) {
  const PastureSward sward = test_sward(2000.0, 500.0);

  // 20 m2/kg on 2000 kg/ha plus 25 m2/kg on 500 kg/ha, over 10000 m2.
  EXPECT_NEAR(sward.leaf_area_index(), ((20.0 * 2000.0) + (25.0 * 500.0)) / 10000.0, 1e-12);
  EXPECT_DOUBLE_EQ(sward.cover_kg_dm(), 2500.0);
  EXPECT_DOUBLE_EQ(sward.legume_fraction(), 500.0 / 2500.0);
}

TEST(PastureSwardTest, InvalidParametersAreRejectedByName) {
  SwardParameters parameters = test_sward_parameters();
  parameters.grass.optimum_temperature_c = 2.0;
  EXPECT_NE(parameters.validation_error().find("cardinal temperatures"), std::string::npos);
  EXPECT_NE(parameters.validation_error().find("ryegrass_perennial"), std::string::npos);
  EXPECT_THROW(PastureSward(parameters, 1000.0, 100.0, 50.0), std::invalid_argument);

  parameters = test_sward_parameters();
  parameters.par_fraction = 1.5;
  EXPECT_NE(parameters.validation_error().find("par_fraction"), std::string::npos);
}

}  // namespace
}  // namespace paddock::core
