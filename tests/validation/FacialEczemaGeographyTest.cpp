// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

/// Facial eczema against two real New Zealand years.
///
/// **The claim being tested is geographic, and it is the strongest one this
/// model makes.** Facial eczema is a disease of the warm damp north. A model
/// that produced it on the Canterbury plains would be wrong in a way no
/// internal check could catch, and one that failed to produce it in the Waikato
/// would be useless. So both are asserted, on recorded weather rather than on
/// weather invented to suit.
///
/// The two sites are not arbitrary. Ruakura is where AgResearch ran the
/// long-term facial eczema selection experiment, and the liver-injury
/// regression this model uses - Morris, Smith and Hickey (2002) - was measured
/// on lambs sired by rams from it. Lincoln is the farm the rest of this
/// project's scenarios run on.

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

#include <paddock/core/Mycotoxin.hpp>
#include <paddock/core/SnapshotWeather.hpp>
#include <paddock/core/Weather.hpp>

namespace paddock::core {
namespace {

MycotoxinParameters facial_eczema() {
  MycotoxinParameters parameters;
  parameters.grass_minimum_temperature_c = 12.0;
  parameters.consecutive_nights = 4;
  parameters.rainfall_mm_per_48h = 4.0;
  parameters.rise_per_favourable_day = 3.0;
  parameters.decay_per_unfavourable_day = 0.93;
  parameters.background_spores_per_g = 2000.0;
  parameters.picograms_per_spore = 1.41;
  parameters.reactor_spore_days = 1'500'000.0;
  parameters.reactor_ggt_iu_per_l = 55.0;
  parameters.liver_injury_intercept = -2.96;
  parameters.liver_injury_ln_ggt_slope = 0.89;
  parameters.clinical_fraction_of_affected = 0.10;
  return parameters;
}

/// Thresholds from data/diseases/facial-eczema.toml, all DairyNZ.
constexpr double kMonitorOwnFarm = 20'000.0;
constexpr double kDangerous = 100'000.0;

WeatherSeries year_at(const std::string& csv) {
  SnapshotWeatherSource::Options options;
  options.path = std::string(PADDOCK_DATA_DIR) + "/scenarios/" + csv;
  options.dataset = "open-meteo";
  options.licence = "CC BY 4.0";

  const SnapshotWeatherSource source(options);
  const std::vector<DailyWeather>& all = source.records();
  WeatherSeries series;
  series.records = all;
  return series;
}

int days_at_or_above(const std::vector<double>& counts, double threshold) {
  return static_cast<int>(
      std::count_if(counts.begin(), counts.end(), [&](double c) { return c >= threshold; }));
}

// The Waikato year. AgResearch's own facial eczema site, on recorded weather.
TEST(FacialEczemaGeographyTest, TheWaikatoYearProducesAnOutbreak) {
  const WeatherSeries weather = year_at("ruakura-fe/weather-2023.csv");
  ASSERT_EQ(weather.records.size(), 366U);

  const std::vector<double> counts = spore_count_series(weather, facial_eczema());
  const double peak = *std::max_element(counts.begin(), counts.end());

  EXPECT_GT(peak, kDangerous)
      << "a Waikato autumn should carry counts past the level guidance calls dangerous; peak "
      << peak;
  EXPECT_GT(days_at_or_above(counts, kMonitorOwnFarm), 20)
      << "and should spend weeks, not days, above the monitoring threshold";
}

// The Canterbury year, on the farm every other scenario here runs. This is the
// assertion that would catch a model that manufactures disease from nothing.
TEST(FacialEczemaGeographyTest, TheCanterburyYearProducesNone) {
  const WeatherSeries weather = year_at("lincoln-lurdf/weather-2023.csv");
  ASSERT_EQ(weather.records.size(), 366U);

  const MycotoxinParameters fe = facial_eczema();
  const std::vector<double> counts = spore_count_series(weather, fe);
  const double peak = *std::max_element(counts.begin(), counts.end());

  EXPECT_DOUBLE_EQ(peak, fe.background_spores_per_g)
      << "Lincoln never gets four consecutive nights at 12 C with rain behind them, so the count "
         "should never leave the background";
  EXPECT_EQ(days_at_or_above(counts, kMonitorOwnFarm), 0);
}

// **And the difference is the weather, not the parameters.** The Canterbury
// result has to survive the fitted rates being wrong, or it is a coincidence
// rather than a finding.
TEST(FacialEczemaGeographyTest, CanterburyStaysClearAcrossTheFittedRange) {
  const WeatherSeries weather = year_at("lincoln-lurdf/weather-2023.csv");

  for (const double rise : {2.5, 3.0, 3.5, 5.0}) {
    for (const double decay : {0.80, 0.88, 0.93, 0.97}) {
      MycotoxinParameters fe = facial_eczema();
      fe.rise_per_favourable_day = rise;
      fe.decay_per_unfavourable_day = decay;

      const std::vector<double> counts = spore_count_series(weather, fe);
      const double peak = *std::max_element(counts.begin(), counts.end());
      EXPECT_DOUBLE_EQ(peak, fe.background_spores_per_g)
          << "rise " << rise << ", decay " << decay << ": Canterbury should stay clear whatever "
          << "the sporulation rates are, because it never opens a run of nights at all";
    }
  }
}

}  // namespace
}  // namespace paddock::core
