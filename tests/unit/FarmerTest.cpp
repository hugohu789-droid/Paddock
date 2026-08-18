// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

// Who decides where the stock go - the last gap in the simulation loop.
//
// The test that matters most here is the one about the shuffle. Smith and
// Dawson (1976) define it as what rotational intent becomes when there are too
// few paddocks for the mobs: "the effect must be to lengthen the grazing period
// or shorten the spelling period". Nothing in this code implements a shuffle.
// The tests below check that it happens anyway on a farm that cannot keep its
// own rules, and does not happen on one that can - which is what it means for a
// behaviour to emerge rather than be coded.

#include <gtest/gtest.h>

#include <cstddef>
#include <vector>

#include <paddock/core/Farmer.hpp>
#include <paddock/core/SyntheticTerrain.hpp>

namespace paddock::core {
namespace {

constexpr double kWest = 1570000.0;
constexpr double kSouth = 5179000.0;
constexpr double kCellSize = 25.0;

BoundingBox area_of(double width_m, double height_m) {
  BoundingBox area = BoundingBox::empty();
  area.expand_to_include(Point2D{kWest, kSouth});
  area.expand_to_include(Point2D{kWest + width_m, kSouth + height_m});
  return area;
}

SoilWaterParameters soil() {
  SoilWaterParameters parameters;
  parameters.total_available_water_mm = 120.0;
  parameters.depletion_fraction = 0.6;
  parameters.crop_coefficient = 0.95;
  parameters.runoff_fraction = 0.05;
  return parameters;
}

SwardParameters sward() {
  SwardParameters parameters;
  parameters.par_fraction = 0.5;
  parameters.decomposition_rate_per_day = 0.02;
  parameters.grass.species_id = "ryegrass_perennial";
  parameters.grass.specific_leaf_area_m2_per_kg = 20.0;
  parameters.grass.extinction_coefficient = 0.5;
  parameters.grass.radiation_use_efficiency_g_per_mj = 1.5;
  parameters.grass.base_temperature_c = 4.0;
  parameters.grass.optimum_temperature_c = 20.0;
  parameters.grass.maximum_temperature_c = 35.0;
  parameters.grass.senescence_rate_per_day = 0.02;
  parameters.grass.residual_kg_dm_per_ha = 1200.0;
  parameters.grass.nitrogen_content_fraction = 0.035;
  parameters.grass.nitrogen_fixation_kg_per_t_dm = 0.0;
  parameters.legume = parameters.grass;
  parameters.legume.species_id = "clover_white";
  parameters.legume.residual_kg_dm_per_ha = 400.0;
  parameters.legume.nitrogen_content_fraction = 0.045;
  parameters.legume.nitrogen_fixation_kg_per_t_dm = 25.0;
  return parameters;
}

FarmletInitialState initial_state() {
  FarmletInitialState state;
  state.soil_water_mm = 90.0;
  state.grass_kg_dm_per_ha = 2600.0;
  state.legume_kg_dm_per_ha = 800.0;
  state.soil_mineral_nitrogen_kg_per_ha = 60.0;
  return state;
}

Mob ewes(int head) {
  Mob mob;
  mob.name = "ewes";
  mob.head = head;
  mob.animal.class_id = "sheep_ewe";
  mob.animal.species_factor = 1.0;
  mob.animal.sex_factor = 1.0;
  mob.animal.standard_reference_weight_kg = 65.0;
  mob.animal.grazing_coefficient = 0.0025;
  mob.animal.gain_energy_ceiling_mj_per_kg = 20.3;
  mob.state.liveweight_kg = 60.0;
  mob.state.age_days = 1200.0;
  return mob;
}

/// A farm of `paddock_count` paddocks, near enough, by dividing the area.
Farm farm_with_paddocks(double width_m, double height_m, double paddock_hectares) {
  const BoundingBox area = area_of(width_m, height_m);
  const Raster<double> elevation = SyntheticElevationSource().fetch(area, kCellSize);
  const Raster<SoilWaterParameters> soils(elevation.cols(), elevation.rows(), elevation.transform(),
                                          soil());

  FarmletGrid grid(soils, sward(), initial_state(), -43.6);
  std::vector<Paddock> paddocks = SyntheticParcelSource(paddock_hectares).fetch(area);
  PaddockMask mask(elevation, paddocks);
  return {std::move(grid), std::move(mask), std::move(paddocks)};
}

/// A calendar that rotates all year: graze three days, spell 35, which is Smith
/// and Dawson's rule for everything but spring.
GrazingCalendar rotation_all_year(const DateRange& run, int spell_days) {
  GrazingRule rule;
  rule.system = GrazingSystem::Rotational;
  rule.maximum_graze_days = 3;
  rule.minimum_spell_days = spell_days;
  return GrazingCalendar(std::vector<GrazingPeriod>{GrazingPeriod{"rotation", run, rule}});
}

GrazingCalendar set_stocking_all_year(const DateRange& run) {
  GrazingRule rule;
  rule.system = GrazingSystem::SetStocking;
  rule.maximum_graze_days = 0;
  rule.minimum_spell_days = 0;
  return GrazingCalendar(std::vector<GrazingPeriod>{GrazingPeriod{"set stocking", run, rule}});
}

Date day_after(const Date& date, int days) {
  return Date::from_days_since_epoch(date.days_since_epoch() + days);
}

DietQuality pasture_diet() {
  DietQuality diet;
  diet.metabolisable_energy_mj_per_kg_dm = 10.5;
  diet.digestibility_percent = 75.0;
  return diet;
}

/// A mild winter day. These tests are about where stock go, not about growth,
/// so the weather only has to be valid - but it does have to be valid: a
/// default-constructed diet is refused by the energy model, and rightly, since
/// feed carrying no energy would need an infinite intake.
DailyWeather quiet_day(const Date& date) {
  DailyWeather weather;
  weather.date = date;
  weather.max_air_temperature_c = 12.0;
  weather.min_air_temperature_c = 5.0;
  weather.rainfall_mm = 0.0;
  weather.solar_radiation_mj_per_m2 = 8.0;
  weather.wind_speed_m_per_s = 2.0;
  return weather;
}

// Under set stocking the farmer does nothing, which is what the system is.
TEST(FarmerTest, SetStockingMovesNobody) {
  const DateRange run{Date{2023, 7, 1}, Date{2024, 6, 30}};
  Farm farm = farm_with_paddocks(800.0, 600.0, 2.0);
  farm.add_mob(ewes(30), 0);

  Farmer farmer(set_stocking_all_year(run));

  for (int day = 0; day < 30; ++day) {
    const Farmer::Day decisions = farmer.decide(farm, day_after(run.first, day));
    EXPECT_TRUE(decisions.moves.empty()) << "on day " << day;
    EXPECT_EQ(decisions.system, GrazingSystem::SetStocking);
  }
  EXPECT_EQ(farm.mobs().front().paddock, 0U) << "the mob never moved";
}

// Rotation moves a mob once it has been somewhere its full graze length, and
// not before.
TEST(FarmerTest, RotationMovesTheMobAfterItsGrazeLength) {
  const DateRange run{Date{2023, 7, 1}, Date{2024, 6, 30}};
  Farm farm = farm_with_paddocks(800.0, 600.0, 2.0);
  farm.add_mob(ewes(30), 0);

  Farmer farmer(rotation_all_year(run, 21));

  // Day 0: the mob has just arrived, nothing to do.
  EXPECT_TRUE(farmer.decide(farm, run.first).moves.empty());

  // Three days of grazing, then it is due to move.
  int moves = 0;
  std::size_t previous = farm.mobs().front().paddock;
  for (int day = 0; day < 12; ++day) {
    const Farmer::Day decisions = farmer.decide(farm, day_after(run.first, day));
    if (!decisions.moves.empty()) {
      ++moves;
      EXPECT_EQ(decisions.moves.front().from, previous);
      EXPECT_NE(decisions.moves.front().to, previous);
      EXPECT_GE(decisions.moves.front().days_grazed, 3);
      previous = decisions.moves.front().to;
    }
    // Advance the farm's own clock without weather: a step is what ages a mob
    // on its paddock.
    farm.step(quiet_day(day_after(run.first, day)), pasture_diet(), nullptr);
  }
  EXPECT_GT(moves, 0) << "a rotating farm has to move stock";
}

// THE SHUFFLE, and the point of the whole design. A farm with plenty of
// paddocks keeps its spell; one with too few for its mobs cannot, and the model
// says so without anything in it implementing a shuffle.
TEST(FarmerTest, TooFewPaddocksForTheMobsProducesTheShuffle) {
  const DateRange run{Date{2023, 7, 1}, Date{2024, 6, 30}};

  // Smith and Dawson's rule: graze three days, spell 35. Holding that needs
  // about twelve paddocks per mob, so four is nowhere near enough and twenty is
  // comfortable.
  const int spell_days = 35;

  Farm cramped = farm_with_paddocks(400.0, 200.0, 2.0);  // 8 ha, 4 paddocks
  cramped.add_mob(ewes(20), 0);

  Farm roomy = farm_with_paddocks(1000.0, 800.0, 2.0);  // 80 ha, 40 paddocks
  roomy.add_mob(ewes(20), 0);

  ASSERT_LT(cramped.paddocks().size(), 8U) << "the cramped farm has to be cramped";
  ASSERT_GT(roomy.paddocks().size(), 20U) << "the roomy farm has to be roomy";

  Farmer cramped_farmer(rotation_all_year(run, spell_days));
  Farmer roomy_farmer(rotation_all_year(run, spell_days));

  // A cold-started farm has nothing rested: days_since_grazed begins at zero
  // everywhere, so for the first spell-length of days *every* move is short on
  // any farm, however well subdivided. That is a start-up transient rather than
  // the shuffle, and counting it would make the two farms look alike. The
  // comparison that means something is the settled one, so the first full
  // rotation is run and discarded.
  const int settling_days = 120;
  const int measured_days = 200;

  int cramped_short = 0;
  int roomy_short = 0;
  for (int day = 0; day < settling_days + measured_days; ++day) {
    const Date date = day_after(run.first, day);
    const int cramped_today = cramped_farmer.decide(cramped, date).short_spells;
    const int roomy_today = roomy_farmer.decide(roomy, date).short_spells;
    if (day >= settling_days) {
      cramped_short += cramped_today;
      roomy_short += roomy_today;
    }
    cramped.step(quiet_day(date), pasture_diet(), nullptr);
    roomy.step(quiet_day(date), pasture_diet(), nullptr);
  }

  EXPECT_GT(cramped_short, 0) << "a farm of " << cramped.paddocks().size()
                              << " paddocks cannot hold a 35 day spell on a three day graze";
  EXPECT_EQ(roomy_short, 0) << "a farm of " << roomy.paddocks().size()
                            << " paddocks can, and did not have to break it";

  GTEST_LOG_(INFO) << "short spells over " << measured_days
                   << " settled days: " << cramped.paddocks().size() << " paddocks -> "
                   << cramped_short << "; " << roomy.paddocks().size() << " paddocks -> "
                   << roomy_short;
}

// The other half of the shuffle: with nowhere free to go, the grazing lengthens
// instead of the spell shortening. One mob per paddock and no spare paddock is
// the extreme case, and it must be reported rather than silently ignored.
TEST(FarmerTest, WithNowhereToGoTheGrazingLengthensInstead) {
  const DateRange run{Date{2023, 7, 1}, Date{2024, 6, 30}};
  Farm farm = farm_with_paddocks(200.0, 200.0, 2.0);

  // A mob on every paddock, so no move is possible.
  ASSERT_GE(farm.paddocks().size(), 1U);
  for (std::size_t paddock = 0; paddock < farm.paddocks().size(); ++paddock) {
    farm.add_mob(ewes(10), paddock);
  }

  Farmer farmer(rotation_all_year(run, 35));

  int extended = 0;
  for (int day = 0; day < 10; ++day) {
    extended += farmer.decide(farm, day_after(run.first, day)).grazings_extended;
    farm.step(quiet_day(day_after(run.first, day)), pasture_diet(), nullptr);
  }

  EXPECT_GT(extended, 0) << "a mob with nowhere to go has to show up as a lengthened grazing";
  for (const FarmMob& mob : farm.mobs()) {
    EXPECT_GT(mob.days_on_paddock, 3) << "and it stayed put";
  }
}

// A mob always goes to the most rested paddock available, which is what makes
// the rotation a rotation rather than a random walk.
TEST(FarmerTest, TheMobGoesToWhicheverFreePaddockHasRestedLongest) {
  const DateRange run{Date{2023, 7, 1}, Date{2024, 6, 30}};
  Farm farm = farm_with_paddocks(800.0, 600.0, 2.0);
  farm.add_mob(ewes(30), 0);

  Farmer farmer(rotation_all_year(run, 21));

  for (int day = 0; day < 40; ++day) {
    const Farmer::Day decisions = farmer.decide(farm, day_after(run.first, day));
    for (const MobMove& move : decisions.moves) {
      // Nothing free was more rested than where it went.
      for (std::size_t paddock = 0; paddock < farm.paddocks().size(); ++paddock) {
        if (paddock == move.to || paddock == move.from) {
          continue;
        }
        if (farm.mob_on(paddock) != Farm::kNobody) {
          continue;
        }
        EXPECT_LE(farm.days_since_grazed()[paddock], move.rest_days)
            << "paddock " << paddock << " was more rested than the one chosen";
      }
    }
    farm.step(quiet_day(day_after(run.first, day)), pasture_diet(), nullptr);
  }
}

}  // namespace
}  // namespace paddock::core
