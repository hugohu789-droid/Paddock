#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include <paddock/core/Simulation.hpp>
#include <paddock/core/SyntheticWeather.hpp>

#include "support/BitPattern.hpp"
#include "support/TestPasture.hpp"
#include "support/TestWeather.hpp"

namespace paddock::core {
namespace {

using test_support::bit_patterns;
using test_support::test_site_parameters;
using test_support::test_soil_parameters;
using test_support::test_sward_parameters;

constexpr std::uint64_t kSeed = 20240701;
constexpr double kLatitude = -43.5;

FarmletInitialState initial_state() {
  FarmletInitialState state;
  state.soil_water_mm = 90.0;
  state.grass_kg_dm_per_ha = 1800.0;
  state.legume_kg_dm_per_ha = 400.0;
  state.soil_mineral_nitrogen_kg_per_ha = 60.0;
  return state;
}

Farmlet test_farmlet() {
  return {test_soil_parameters(), test_sward_parameters(), initial_state(), kLatitude};
}

std::vector<double> cover_series(const RunResult& result) {
  std::vector<double> cover;
  cover.reserve(result.daily.size());
  for (const DailyRecord& day : result.daily) {
    cover.push_back(day.cover_kg_dm);
  }
  return cover;
}

TEST(FarmletTest, AYearProducesOneRecordPerDayAndClosesItsBudgets) {
  Farmlet farmlet = test_farmlet();
  const SyntheticWeatherSource weather(test_site_parameters(), kSeed);

  const RunResult result = run(farmlet, weather, DateRange::calendar_year(2023));

  ASSERT_EQ(result.daily.size(), 365U);
  EXPECT_EQ(result.summary.days, 365);
  EXPECT_EQ(result.daily.front().date, (Date{2023, 1, 1}));
  EXPECT_EQ(result.daily.back().date, (Date{2023, 12, 31}));
  EXPECT_TRUE(result.budgets_close(farmlet))
      << result.ledger.report(Budget::Water, farmlet.soil().water_mm())
      << result.ledger.report(Budget::DryMatter, farmlet.sward().cover_kg_dm())
      << result.ledger.report(Budget::Nitrogen, farmlet.sward().total_nitrogen_kg());
}

TEST(FarmletTest, TheSummaryAgreesWithTheDailyRecordsAndTheLedger) {
  Farmlet farmlet = test_farmlet();
  const SyntheticWeatherSource weather(test_site_parameters(), kSeed);

  const RunResult result = run(farmlet, weather, DateRange::calendar_year(2023));

  double rainfall = 0.0;
  double growth = 0.0;
  for (const DailyRecord& day : result.daily) {
    rainfall += day.rainfall_mm;
    growth += day.growth_kg_dm;
  }

  EXPECT_NEAR(result.summary.total_rainfall_mm, rainfall, 1e-9);
  EXPECT_NEAR(result.summary.total_growth_kg_dm, growth, 1e-9);
  EXPECT_NEAR(result.summary.total_rainfall_mm, result.ledger.total_inflow(Budget::Water), 1e-9);
  EXPECT_NEAR(result.summary.total_nitrogen_fixed_kg, result.ledger.total_inflow(Budget::Nitrogen),
              1e-12);
  EXPECT_DOUBLE_EQ(result.summary.closing_soil_water_mm, farmlet.soil().water_mm());
  EXPECT_DOUBLE_EQ(result.summary.closing_cover_kg_dm, farmlet.sward().cover_kg_dm());
}

TEST(FarmletTest, WeatherProvenanceTravelsWithTheResult) {
  Farmlet farmlet = test_farmlet();
  const SyntheticWeatherSource weather(test_site_parameters(), kSeed);

  const RunResult result = run(farmlet, weather, DateRange::calendar_year(2023));

  EXPECT_EQ(result.weather_provenance.source_name, "synthetic");
  EXPECT_EQ(result.weather_provenance.dataset, "test_site");
  EXPECT_EQ(result.weather_provenance.content_hash.size(), 64U);
}

// The property the scenario bundle format rests on.
TEST(FarmletTest, TheSameSeedGivesABitIdenticalYear) {
  const auto run_year = [](std::uint64_t seed) {
    Farmlet farmlet = test_farmlet();
    const SyntheticWeatherSource weather(test_site_parameters(), seed);
    return cover_series(run(farmlet, weather, DateRange::calendar_year(2023)));
  };

  EXPECT_EQ(bit_patterns(run_year(kSeed)), bit_patterns(run_year(kSeed)));
  EXPECT_NE(bit_patterns(run_year(kSeed)), bit_patterns(run_year(kSeed + 1)));
}

// Rain reaches tomorrow's growth, not today's.
//
// FAO-56 takes the water stress coefficient from the root zone depletion the
// previous day's balance left behind (Eq. 84), so a shower does not relieve
// stress retroactively - a farm that was dry this morning was dry all day. The
// rain shows up the next morning. This test exists because the first version of
// it asserted the opposite, and the model was right.
TEST(FarmletTest, RainLiftsTomorrowsGrowthRatherThanTodays) {
  const FarmletInitialState empty_profile{0.0, 1800.0, 400.0, 60.0};
  Farmlet rained_on(test_soil_parameters(), test_sward_parameters(), empty_profile, kLatitude);
  Farmlet stayed_dry(test_soil_parameters(), test_sward_parameters(), empty_profile, kLatitude);

  DailyWeather first_day;
  first_day.date = Date{2023, 11, 15};
  first_day.solar_radiation_mj_per_m2 = 20.0;
  first_day.min_air_temperature_c = 10.0;
  first_day.max_air_temperature_c = 22.0;
  DailyWeather wet_first_day = first_day;
  wet_first_day.rainfall_mm = 40.0;

  const DailyRecord wet_today = rained_on.step(wet_first_day);
  const DailyRecord dry_today = stayed_dry.step(first_day);

  EXPECT_DOUBLE_EQ(wet_today.water_stress_coefficient, dry_today.water_stress_coefficient);
  EXPECT_DOUBLE_EQ(wet_today.growth_kg_dm, dry_today.growth_kg_dm);
  EXPECT_GT(wet_today.soil_water_mm, dry_today.soil_water_mm);

  DailyWeather second_day = first_day;
  second_day.date = Date{2023, 11, 16};
  const DailyRecord wet_tomorrow = rained_on.step(second_day);
  const DailyRecord dry_tomorrow = stayed_dry.step(second_day);

  EXPECT_GT(wet_tomorrow.water_stress_coefficient, dry_tomorrow.water_stress_coefficient);
  EXPECT_GT(wet_tomorrow.growth_kg_dm, dry_tomorrow.growth_kg_dm);
}

}  // namespace
}  // namespace paddock::core
