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

#include <paddock/config/DiseaseConfig.hpp>
#include <paddock/config/DiseaseReport.hpp>
#include <paddock/core/Mycotoxin.hpp>
#include <paddock/core/SnapshotWeather.hpp>
#include <paddock/core/Weather.hpp>

namespace paddock::core {
namespace {

/// **Read from data/diseases/facial-eczema.toml, never copied out of it.**
///
/// The values used to be written out here as a C++ struct, and within hours of
/// the file being created the two had drifted: the file said one sporulation
/// rate and the test asserted another, and every test passed. A disease is data
/// in this project, so a test of it reads the data.
MycotoxinParameters facial_eczema() {
  return config::load_disease(std::string(PADDOCK_DATA_DIR) + "/diseases/facial-eczema.toml")
      .mycotoxin;
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

// **Ten consecutive years at one farm.** A single year cannot tell you whether
// a model is bounded, and this project found that out the hard way: the year
// first tested had no favourable run longer than six nights, and every test was
// green against a model whose spore count ran to 4.8e18 on weather that had
// actually happened. A decade is what caught it.
//
// What this asserts is the shape of a real facial eczema record rather than any
// one year's numbers: the disease is not every year, it is not never, and no
// year is physically absurd.
TEST(FacialEczemaGeographyTest, ADecadeAtRuakuraLooksLikeARecordRatherThanAnOverflow) {
  const WeatherSeries weather = year_at("ruakura-fe/weather-2015-2025.csv");
  ASSERT_EQ(weather.records.size(), 3653U) << "ten farm years, July 2015 to June 2025";

  const MycotoxinParameters fe = facial_eczema();
  const std::vector<MycotoxinYear> years =
      mycotoxin_years(weather, kMonitorOwnFarm, kDangerous, fe);

  ASSERT_EQ(years.size(), 10U) << "July 2015 to June 2025 is ten whole farm years";

  int clear = 0;
  int reached_monitoring = 0;
  int reached_dangerous = 0;
  for (const MycotoxinYear& year : years) {
    // **No year may be physically absurd.** This is the assertion the decade
    // was fetched for.
    EXPECT_LE(year.peak_spores_per_g, fe.carrying_capacity_spores_per_g)
        << year.starting_year << " ran past what litter can carry";

    if (year.days_at_or_above_monitoring == 0) {
      ++clear;
    } else {
      ++reached_monitoring;
    }
    if (year.days_at_or_above_dangerous > 0) {
      ++reached_dangerous;
    }
  }

  EXPECT_GT(clear, 0) << "a decade in the Waikato should contain a year that was simply fine";
  EXPECT_GT(reached_monitoring, 4) << "and most years should ask a farmer to start counting";
  EXPECT_GT(reached_dangerous, 2) << "and several should reach the level guidance calls dangerous";
  EXPECT_LT(reached_dangerous, static_cast<int>(years.size()))
      << "but not every year, or the model is describing a place rather than a season";
}

// Exposure crossing the July boundary is what makes a decade more than ten
// separate years, so this compares a year run inside the decade against the
// same year run from a standing start. If the two agree, exposure is resetting
// and the decade is ten independent runs wearing a trench coat.
TEST(FacialEczemaGeographyTest, AYearInsideTheDecadeInheritsFromTheOneBeforeIt) {
  const WeatherSeries decade = year_at("ruakura-fe/weather-2015-2025.csv");
  const MycotoxinParameters fe = facial_eczema();

  const std::vector<MycotoxinYear> together =
      mycotoxin_years(decade, kMonitorOwnFarm, kDangerous, fe);
  ASSERT_FALSE(together.empty());

  // Pick the year that follows the worst one, which is where an inheritance
  // would show most clearly.
  std::size_t worst = 0;
  for (std::size_t i = 0; i < together.size(); ++i) {
    if (together[i].peak_ggt_iu_per_l > together[worst].peak_ggt_iu_per_l) {
      worst = i;
    }
  }
  ASSERT_LT(worst + 1, together.size()) << "the worst year is the last, so nothing follows it";
  const MycotoxinYear& following = together[worst + 1];

  // The same year on its own: every day whose farm year matches, and nothing
  // before it.
  WeatherSeries alone;
  for (const DailyWeather& day : decade.records) {
    const int starting_year = day.date.month >= 7 ? day.date.year : day.date.year - 1;
    if (starting_year == following.starting_year) {
      alone.records.push_back(day);
    }
  }
  ASSERT_FALSE(alone.records.empty());

  const std::vector<MycotoxinYear> by_itself =
      mycotoxin_years(alone, kMonitorOwnFarm, kDangerous, fe);
  ASSERT_EQ(by_itself.size(), 1U);

  // The spore counts are the same weather either way; only the liver differs.
  EXPECT_DOUBLE_EQ(following.peak_spores_per_g, by_itself.front().peak_spores_per_g)
      << "the same weather should grow the same spores whichever run it is in";
  EXPECT_GT(following.peak_ggt_iu_per_l, by_itself.front().peak_ggt_iu_per_l)
      << "a year that follows a bad one should start with something already carried";
}

// **The report a person reads.** These check what it says, not how it is
// spelled: a report whose headline sentence disagreed with its own table would
// be worse than no report.
TEST(FacialEczemaGeographyTest, TheDecadeReportLeadsWithTheDecision) {
  config::DiseaseSite ruakura;
  ruakura.name = "Ruakura";
  ruakura.weather = year_at("ruakura-fe/weather-2015-2025.csv");

  const config::DiseaseDefinition fe =
      config::load_disease(std::string(PADDOCK_DATA_DIR) + "/diseases/facial-eczema.toml");
  const std::string report = config::render_disease_years(ruakura, fe);

  // The lead is the decision, so a reader who stops after one sentence has it.
  EXPECT_NE(report.find("a zinc programme would have run in"), std::string::npos);
  EXPECT_NE(report.find("2015-16"), std::string::npos) << "every year appears";
  EXPECT_NE(report.find("2024-25"), std::string::npos);

  // **The caveat is not optional.** A report that showed zinc days without
  // saying the model never gave any zinc would be claiming a treated farm.
  EXPECT_NE(report.find("does not give the animals zinc"), std::string::npos);

  // **Found by reading the output rather than the assertions.** A programme
  // that opens in one year and is still running in the next has no start date
  // of its own, and the report printed the unset one - 1970-01-01 - while every
  // test passed.
  EXPECT_EQ(report.find("1970"), std::string::npos)
      << "a year that inherited a running programme is printing an unset date";
  // Every year that ran a programme names a date or says the season was carried
  // in; none of them prints an unset one. The old code lost the start date of a
  // year that both inherited a running programme and later opened a new one,
  // which is 2023-24 here.
  EXPECT_NE(report.find("2024-01-01"), std::string::npos)
      << "2023-24 inherited a programme and then opened its own; the second start is the "
         "one to print";
}

TEST(FacialEczemaGeographyTest, TheFarmComparisonSaysWhetherItIsAQuestionAtAll) {
  config::DiseaseSite ruakura;
  ruakura.name = "Ruakura (Waikato)";
  ruakura.weather = year_at("ruakura-fe/weather-2015-2025.csv");

  config::DiseaseSite lincoln;
  lincoln.name = "Lincoln (Canterbury)";
  lincoln.weather = year_at("lincoln-lurdf/weather-2015-2025.csv");

  const config::DiseaseDefinition fe =
      config::load_disease(std::string(PADDOCK_DATA_DIR) + "/diseases/facial-eczema.toml");
  const std::string report = config::render_disease_comparison({ruakura, lincoln}, fe);

  EXPECT_NE(report.find("Ruakura (Waikato)"), std::string::npos);
  EXPECT_NE(report.find("Lincoln (Canterbury)"), std::string::npos);
  EXPECT_NE(report.find("Years needing a programme"), std::string::npos);
  EXPECT_NE(report.find("0 of 10"), std::string::npos)
      << "Canterbury should need no programme in any of the ten years";
}

// The claim the comparison rests on, asserted on the numbers rather than on the
// rendered text: ten Canterbury years never reach the count at which the
// guidance asks a farmer to start counting.
TEST(FacialEczemaGeographyTest, TenCanterburyYearsNeverReachTheMonitoringThreshold) {
  const WeatherSeries weather = year_at("lincoln-lurdf/weather-2015-2025.csv");
  ASSERT_EQ(weather.records.size(), 3653U);

  const MycotoxinParameters fe = facial_eczema();
  const std::vector<MycotoxinYear> years =
      mycotoxin_years(weather, kMonitorOwnFarm, kDangerous, fe);

  for (const MycotoxinYear& year : years) {
    EXPECT_EQ(year.zinc_programme_days, 0)
        << year.starting_year << " called for zinc in Canterbury";
    EXPECT_LT(year.peak_spores_per_g, kMonitorOwnFarm)
        << year.starting_year << " reached the monitoring threshold";
  }
}

}  // namespace
}  // namespace paddock::core
