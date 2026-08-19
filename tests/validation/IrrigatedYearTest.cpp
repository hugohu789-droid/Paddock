// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

// The same farm, the same year, once rain-fed and once irrigated.
//
// This is the demonstration the irrigation work exists for, and it is a
// DIFFERENCE rather than a pair of predictions. Both runs use the same model,
// the same soil and the same weather; one input changes. That difference is
// informative even though neither absolute figure is quotable, because the
// sward parameters these farms ship with are placeholders (docs/verify.md) -
// so this file asserts direction and cost, and never a yield.
//
// The weather is the real Lincoln year, so the seasonal shape driving it is
// the one that actually happened rather than a generator's idea of Canterbury.

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

#include <paddock/config/ScenarioConfig.hpp>
#include <paddock/core/Irrigation.hpp>
#include <paddock/core/Pasture.hpp>
#include <paddock/core/SoilWater.hpp>
#include <paddock/core/Weather.hpp>

namespace paddock::config {
namespace {

constexpr double kLatitude = -43.641;
constexpr double kHectares = 79.0;  ///< Lincoln University's research dairy farm.

std::string bundle_path() {
  return std::string(PADDOCK_DATA_DIR) + "/scenarios/lincoln-lurdf";
}

/// What one year over one hectare came to.
struct Year {
  double growth_kg_dm = 0.0;
  int stressed_days = 0;
  core::IrrigationTally water;

  [[nodiscard]] double growth_t_dm_per_ha() const { return growth_kg_dm / 1000.0; }
};

/// Runs one hectare for the bundle's year under one irrigation rule.
///
/// The soil and the sward are driven directly rather than through the farm,
/// because what is being demonstrated is the water and the growth: no stock,
/// no paddocks, nothing that would put another difference between the two
/// runs.
Year run_year(const ScenarioBundle& bundle, const core::IrrigationPolicy& policy,
              const core::IrrigationSystem& system) {
  core::SoilWaterBucket soil(bundle.soil, 90.0);
  core::PastureSward sward(bundle.sward, 1800.0, 400.0, 60.0);

  Year year;
  int days_since_last = 999;
  for (const core::DailyWeather& day : bundle.weather->fetch(bundle.range).records) {
    // **The soil reports; the rule decides; the caller applies.** Nothing in
    // SoilWaterBucket knows this decision is being made.
    const core::IrrigationDecision decision =
        core::decide_irrigation(soil.depletion_mm(), soil.parameters().total_available_water_mm,
                                days_since_last, policy, system);
    year.water.record(decision);
    days_since_last = decision.irrigate ? 0 : days_since_last + 1;

    const core::SoilWaterFluxes water =
        soil.step(day, kLatitude, 1.0, nullptr, decision.effective_mm);
    if (water.stress_coefficient < 0.999) {
      ++year.stressed_days;
    }
    year.growth_kg_dm += sward.step(day, water.stress_coefficient).total_growth_kg_dm();
  }
  return year;
}

core::IrrigationPolicy irrigating() {
  core::IrrigationPolicy policy;
  policy.enabled = true;
  // FAO-56 Table 22 gives p = 0.6 for grazed pasture; watering a little before
  // that keeps the profile off the point where growth is held back.
  policy.trigger_depletion_fraction = 0.5;
  policy.target_depletion_fraction = 0.15;
  policy.maximum_application_mm = 25.0;
  policy.minimum_return_days = 3;
  return policy;
}

TEST(IrrigatedYearTest, IrrigationBuysGrowthAndTheWaterIsCounted) {
  const ScenarioBundle bundle = load_scenario(bundle_path());

  const Year dry = run_year(bundle, {}, {});
  const Year wet = run_year(bundle, irrigating(), {});

  // Every one of these is a direction, not a prediction.
  EXPECT_GT(wet.growth_kg_dm, dry.growth_kg_dm) << "water that relieves stress has to buy growth";
  EXPECT_LT(wet.stressed_days, dry.stressed_days)
      << "and the point of applying it is fewer days short of water";
  EXPECT_GT(wet.water.events, 0) << "a Canterbury year that never triggered would make this "
                                    "test about the trigger rather than about irrigation";
  EXPECT_EQ(dry.water.events, 0) << "the rain-fed run must not water anything";
  EXPECT_DOUBLE_EQ(dry.water.effective_mm, 0.0);

  // Water productivity: the additional dry matter over the water it took. The
  // metric the roadmap calls the useful one, and it is a ratio of two
  // differences rather than a yield, which is why it can be computed at all
  // while the sward parameters are placeholders.
  const double extra_kg_dm = wet.growth_kg_dm - dry.growth_kg_dm;
  const double water_m3_per_ha = wet.water.pumped_m3_per_ha;
  ASSERT_GT(water_m3_per_ha, 0.0);
  const double productivity = extra_kg_dm / water_m3_per_ha;

  GTEST_LOG_(INFO) << "rain-fed " << dry.growth_t_dm_per_ha() << " t DM/ha over "
                   << dry.stressed_days << " stressed days; irrigated " << wet.growth_t_dm_per_ha()
                   << " t DM/ha over " << wet.stressed_days << " stressed days on "
                   << wet.water.effective_mm << " mm in " << wet.water.events << " events ("
                   << wet.water.pumped_megalitres(kHectares) << " ML over " << kHectares
                   << " ha); water productivity " << productivity << " kg DM per m3";

  EXPECT_GT(productivity, 0.0);
}

// Watering harder costs more water for less and less extra grass. That is the
// shape the roadmap wants a reader to see, and it is a property of the model
// rather than a number out of it.
TEST(IrrigatedYearTest, MoreWaterReturnsLessAndLessExtraGrass) {
  const ScenarioBundle bundle = load_scenario(bundle_path());

  const core::IrrigationPolicy moderate = irrigating();
  core::IrrigationPolicy generous = irrigating();
  // Water earlier and refill closer to full: more water, on the same farm.
  generous.trigger_depletion_fraction = 0.25;
  generous.target_depletion_fraction = 0.05;

  const Year dry = run_year(bundle, {}, {});
  const Year some = run_year(bundle, moderate, {});
  const Year lots = run_year(bundle, generous, {});

  ASSERT_GT(lots.water.effective_mm, some.water.effective_mm);
  ASSERT_GT(some.growth_kg_dm, dry.growth_kg_dm);

  const double first_return = (some.growth_kg_dm - dry.growth_kg_dm) / some.water.effective_mm;
  const double second_return = (lots.growth_kg_dm - some.growth_kg_dm) /
                               std::max(1e-9, lots.water.effective_mm - some.water.effective_mm);

  GTEST_LOG_(INFO) << "first " << some.water.effective_mm << " mm returned " << first_return
                   << " kg DM/mm; the next " << (lots.water.effective_mm - some.water.effective_mm)
                   << " mm returned " << second_return << " kg DM/mm";

  EXPECT_LT(second_return, first_return)
      << "the second lot of water should buy less than the first";
}

}  // namespace
}  // namespace paddock::config
