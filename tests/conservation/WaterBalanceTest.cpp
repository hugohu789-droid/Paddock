// The water budget, now that there is a real process to account for.
//
// A year of weather goes into a soil water bucket; rainfall, runoff,
// evapotranspiration and drainage come out. Opening storage plus inflows minus
// outflows must equal the water the profile actually holds, to within 1e-9,
// over 365 simulated days. Unlike the placeholder suite this exercises the
// process itself - if the bucket ever loses a millimetre between the flows it
// reports and the state it keeps, this fails.

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include <paddock/core/BudgetLedger.hpp>
#include <paddock/core/SimulationClock.hpp>
#include <paddock/core/SoilWater.hpp>
#include <paddock/core/SyntheticWeather.hpp>
#include <paddock/core/Weather.hpp>

#include "support/BitPattern.hpp"
#include "support/TestWeather.hpp"

namespace paddock::core {
namespace {

using test_support::bit_patterns;
using test_support::test_site_parameters;

constexpr std::uint64_t kMasterSeed = 20240701;
constexpr double kLatitude = -43.5;

SoilWaterParameters soil_parameters() {
  SoilWaterParameters parameters;
  // 1000 (0.38 - 0.18) x 0.6 m, the shape of a moderately deep silt loam.
  parameters.total_available_water_mm = SoilWaterParameters::total_available_water(0.38, 0.18, 0.6);
  parameters.depletion_fraction = 0.6;  // FAO-56 Table 22, grazed pasture
  parameters.crop_coefficient = 0.95;   // FAO-56 Table 12, rotated grazing
  parameters.runoff_fraction = 0.05;
  return parameters;
}

WeatherSeries test_year(int year) {
  const SyntheticWeatherSource source(test_site_parameters(), kMasterSeed);
  return source.fetch(DateRange::calendar_year(year));
}

TEST(WaterConservationTest, AYearOfSoilWaterBalances) {
  BudgetLedger ledger;
  SoilWaterBucket bucket(soil_parameters(), 90.0);
  ledger.set_opening_stock(Budget::Water, bucket.water_mm());

  const WeatherSeries year = test_year(2023);
  ASSERT_EQ(year.size(), 365U);
  for (const DailyWeather& weather : year.records) {
    bucket.step(weather, kLatitude, &ledger);
  }

  EXPECT_TRUE(ledger.closes(Budget::Water, bucket.water_mm()))
      << ledger.report(Budget::Water, bucket.water_mm());
  EXPECT_DOUBLE_EQ(ledger.total_inflow(Budget::Water), year.total_rainfall_mm());
  EXPECT_GT(ledger.total_outflow(Budget::Water), 0.0);
}

// The same year, run day by day with the ledger reset each day, must close on
// every one of them. A budget that only closes over a year can hide a process
// that borrows water in summer and repays it in winter.
TEST(WaterConservationTest, EveryIndividualDayBalances) {
  SoilWaterBucket bucket(soil_parameters(), 90.0);

  for (const DailyWeather& weather : test_year(2023).records) {
    BudgetLedger daily;
    daily.set_opening_stock(Budget::Water, bucket.water_mm());
    bucket.step(weather, kLatitude, &daily);
    ASSERT_TRUE(daily.closes(Budget::Water, bucket.water_mm()))
        << weather.date.to_iso_string() << '\n'
        << daily.report(Budget::Water, bucket.water_mm());
  }
}

// Negative control: the gate has to be able to fail. Drainage is the flow most
// easily forgotten, because it leaves the farm without anyone seeing it.
TEST(WaterConservationTest, AnUnreportedDrainageIsDetected) {
  BudgetLedger ledger;
  SoilWaterBucket bucket(soil_parameters(), 90.0);
  ledger.set_opening_stock(Budget::Water, bucket.water_mm());
  double unreported = 0.0;

  for (const DailyWeather& weather : test_year(2023).records) {
    const SoilWaterFluxes fluxes = bucket.step(weather, kLatitude);
    ledger.record_inflow(Budget::Water, "rainfall", fluxes.rainfall_mm);
    ledger.record_outflow(Budget::Water, "runoff", fluxes.runoff_mm);
    ledger.record_outflow(Budget::Water, "evapotranspiration", fluxes.evapotranspiration_mm);
    unreported += fluxes.drainage_mm;  // deliberately not recorded
  }

  ASSERT_GT(unreported, 1.0) << "the test year produced no drainage to lose";
  EXPECT_FALSE(ledger.closes(Budget::Water, bucket.water_mm()));
  EXPECT_NEAR(ledger.residual(Budget::Water, bucket.water_mm()), -unreported, 1e-6);
}

TEST(WaterConservationTest, TheSameSeedGivesTheSameYearOfSoilWater) {
  const auto run_year = [](std::uint64_t seed) {
    const SyntheticWeatherSource source(test_site_parameters(), seed);
    SoilWaterBucket bucket(soil_parameters(), 90.0);
    std::vector<double> daily_water;
    daily_water.reserve(365);
    for (const DailyWeather& weather : source.fetch(DateRange::calendar_year(2023)).records) {
      bucket.step(weather, kLatitude);
      daily_water.push_back(bucket.water_mm());
    }
    return daily_water;
  };

  EXPECT_EQ(bit_patterns(run_year(kMasterSeed)), bit_patterns(run_year(kMasterSeed)));
  EXPECT_NE(bit_patterns(run_year(kMasterSeed)), bit_patterns(run_year(kMasterSeed + 1)));
}

// A pastoral season, not just an accounting identity: the profile should refill
// over a Canterbury winter and draw down over summer.
TEST(WaterConservationTest, TheProfileFillsInWinterAndDrawsDownInSummer) {
  SoilWaterBucket bucket(soil_parameters(), 90.0);
  double end_of_winter = 0.0;
  double end_of_summer = 0.0;

  for (const DailyWeather& weather : test_year(2023).records) {
    bucket.step(weather, kLatitude);
    if (weather.date.month == 8 && weather.date.day == 31) {
      end_of_winter = bucket.water_mm();
    }
    if (weather.date.month == 2 && weather.date.day == 28) {
      end_of_summer = bucket.water_mm();
    }
  }

  EXPECT_GT(end_of_winter, end_of_summer);
}

}  // namespace
}  // namespace paddock::core
