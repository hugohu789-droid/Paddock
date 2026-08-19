// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

// The water budget, now that there is a real process to account for.
//
// A year of weather goes into a soil water bucket; rainfall, runoff,
// evapotranspiration and drainage come out. Opening storage plus inflows minus
// outflows must equal the water the profile actually holds, to within 1e-9,
// over 365 simulated days. Unlike the placeholder suite this exercises the
// process itself - if the bucket ever loses a millimetre between the flows it
// reports and the state it keeps, this fails.

#include <gtest/gtest.h>

#include <algorithm>
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
    bucket.step(weather, kLatitude, 1.0, &ledger);
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
    bucket.step(weather, kLatitude, 1.0, &daily);
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

// Irrigation is water like any other, and the budget has to say so.
//
// A new inflow that the ledger does not know about is the exact failure this
// gate exists for: the profile would gain water the accounts cannot explain,
// and every drainage and leaching number downstream would be quietly wrong.
TEST(WaterConservationTest, AYearOfIrrigatedSoilWaterBalances) {
  const WeatherSeries year = test_year(2024);
  SoilWaterBucket soil(soil_parameters(), 60.0);
  BudgetLedger ledger;
  ledger.set_opening_stock(Budget::Water, soil.water_mm());

  double applied_mm = 0.0;
  for (const DailyWeather& day : year.records) {
    // The textbook rule: water when the profile has been drawn down to the
    // readily available water, and put back what is missing. FAO-56 Eq. 83.
    const double irrigation =
        soil.depletion_mm() >= soil.readily_available_water_mm() ? soil.depletion_mm() : 0.0;
    applied_mm += irrigation;
    soil.step(day, kLatitude, 1.0, &ledger, irrigation);
  }

  EXPECT_GT(applied_mm, 0.0) << "a Canterbury year that never triggered the rule would mean the "
                                "trigger, not the balance, is what this test measured";
  EXPECT_TRUE(ledger.closes(Budget::Water, soil.water_mm()))
      << ledger.report(Budget::Water, soil.water_mm());
  EXPECT_DOUBLE_EQ(ledger.total_inflow(Budget::Water), year.total_rainfall_mm() + applied_mm)
      << "every millimetre put on has to appear in the accounts";
}

// And that it does the thing it is for: water put on is water the pasture is
// not short of.
TEST(WaterConservationTest, IrrigatingRelievesTheStressItWasAppliedFor) {
  const WeatherSeries year = test_year(2024);

  SoilWaterBucket dry(soil_parameters(), 60.0);
  SoilWaterBucket watered(soil_parameters(), 60.0);

  double driest_stress = 1.0;
  double watered_stress = 1.0;
  double applied_mm = 0.0;
  for (const DailyWeather& day : year.records) {
    driest_stress = std::min(driest_stress, dry.step(day, kLatitude).stress_coefficient);

    const double irrigation = watered.depletion_mm() >= watered.readily_available_water_mm()
                                  ? watered.depletion_mm()
                                  : 0.0;
    applied_mm += irrigation;
    watered_stress = std::min(
        watered_stress, watered.step(day, kLatitude, 1.0, nullptr, irrigation).stress_coefficient);
  }

  EXPECT_LT(driest_stress, 1.0) << "the unirrigated year never went short, so there was nothing "
                                   "for irrigation to relieve";
  EXPECT_GT(watered_stress, driest_stress)
      << "unirrigated stress fell to " << driest_stress << ", irrigated to " << watered_stress
      << " on " << applied_mm << " mm";
}

// Over-applying is not free. Water past field capacity leaves as drainage, and
// it has to be reported rather than absorbed.
TEST(WaterConservationTest, WaterPutOnAFullProfileDrainsAndIsCounted) {
  const WeatherSeries year = test_year(2024);
  SoilWaterBucket soil(soil_parameters(), soil_parameters().total_available_water_mm);
  BudgetLedger ledger;
  ledger.set_opening_stock(Budget::Water, soil.water_mm());

  const SoilWaterFluxes fluxes = soil.step(year.records.front(), kLatitude, 1.0, &ledger, 50.0);

  EXPECT_DOUBLE_EQ(fluxes.irrigation_mm, 50.0);
  EXPECT_GT(fluxes.drainage_mm, 0.0) << "50 mm onto a full profile has to go somewhere";
  EXPECT_TRUE(ledger.closes(Budget::Water, soil.water_mm()))
      << ledger.report(Budget::Water, soil.water_mm());
}

}  // namespace paddock::core
