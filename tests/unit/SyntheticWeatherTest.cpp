#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include <paddock/core/SimulationClock.hpp>
#include <paddock/core/SyntheticWeather.hpp>

#include "support/BitPattern.hpp"
#include "support/TestWeather.hpp"

namespace paddock::core {
namespace {

using test_support::bit_patterns;
using test_support::test_site_parameters;

constexpr std::uint64_t kSeed = 20240701;

std::vector<double> rainfall_of(const WeatherSeries& series) {
  std::vector<double> rainfall;
  rainfall.reserve(series.size());
  for (const DailyWeather& record : series.records) {
    rainfall.push_back(record.rainfall_mm);
  }
  return rainfall;
}

TEST(SyntheticWeatherTest, AYearIsWellFormedAndCarriesProvenance) {
  const SyntheticWeatherSource source(test_site_parameters(), kSeed);

  const WeatherSeries year = source.fetch(DateRange::calendar_year(2023));

  EXPECT_EQ(year.size(), 365U);
  EXPECT_TRUE(year.is_well_formed());
  EXPECT_EQ(year.records.front().date, (Date{2023, 1, 1}));
  EXPECT_EQ(year.records.back().date, (Date{2023, 12, 31}));
  EXPECT_EQ(year.provenance.source_name, "synthetic");
  EXPECT_EQ(year.provenance.dataset, "test_site");
  EXPECT_EQ(year.provenance.content_hash.size(), 64U);  // SHA-256 in hex
}

TEST(SyntheticWeatherTest, TheSameSeedGivesTheSameYear) {
  const SyntheticWeatherSource first(test_site_parameters(), kSeed);
  const SyntheticWeatherSource second(test_site_parameters(), kSeed);
  const SyntheticWeatherSource other(test_site_parameters(), kSeed + 1);

  const std::vector<double> from_first = rainfall_of(first.fetch(DateRange::calendar_year(2023)));

  EXPECT_EQ(bit_patterns(from_first),
            bit_patterns(rainfall_of(second.fetch(DateRange::calendar_year(2023)))));
  EXPECT_NE(bit_patterns(from_first),
            bit_patterns(rainfall_of(other.fetch(DateRange::calendar_year(2023)))));
}

// The property that makes the generator usable inside a scenario bundle: a run
// that starts in March sees exactly the March a full-year run would have seen,
// because each day is keyed by its date and not by its position in a loop.
TEST(SyntheticWeatherTest, ASubrangeMatchesTheSameDaysOfTheWholeYear) {
  const SyntheticWeatherSource source(test_site_parameters(), kSeed);

  const WeatherSeries year = source.fetch(DateRange::calendar_year(2023));
  const WeatherSeries march = source.fetch(DateRange{Date{2023, 3, 1}, Date{2023, 3, 31}});

  ASSERT_EQ(march.size(), 31U);
  const std::size_t offset = static_cast<std::size_t>(Date{2023, 3, 1}.days_since_epoch() -
                                                      Date{2023, 1, 1}.days_since_epoch());
  for (std::size_t day = 0; day < march.size(); ++day) {
    EXPECT_EQ(march.records[day].date, year.records[offset + day].date);
    EXPECT_EQ(bit_patterns(std::vector<double>{march.records[day].rainfall_mm}),
              bit_patterns(std::vector<double>{year.records[offset + day].rainfall_mm}));
    EXPECT_EQ(bit_patterns(std::vector<double>{march.records[day].max_air_temperature_c}),
              bit_patterns(std::vector<double>{year.records[offset + day].max_air_temperature_c}));
  }
}

TEST(SyntheticWeatherTest, SeasonsRunTheSouthernWayRound) {
  const SyntheticWeatherSource source(test_site_parameters(), kSeed);

  const WeatherSeries january = source.fetch(DateRange{Date{2023, 1, 1}, Date{2023, 1, 31}});
  const WeatherSeries july = source.fetch(DateRange{Date{2023, 7, 1}, Date{2023, 7, 31}});

  double january_mean = 0.0;
  for (const DailyWeather& record : january.records) {
    january_mean += record.mean_air_temperature_c() / 31.0;
  }
  double july_mean = 0.0;
  for (const DailyWeather& record : july.records) {
    july_mean += record.mean_air_temperature_c() / 31.0;
  }

  EXPECT_GT(january_mean, july_mean + 5.0);
}

TEST(SyntheticWeatherTest, WetDaysAreCloudier) {
  const SyntheticWeatherSource source(test_site_parameters(), kSeed);
  double wet_radiation = 0.0;
  double dry_radiation = 0.0;
  int wet_days = 0;
  int dry_days = 0;

  for (const DailyWeather& record : source.fetch(DateRange::calendar_year(2023)).records) {
    if (record.rainfall_mm > 0.0) {
      wet_radiation += record.solar_radiation_mj_per_m2;
      ++wet_days;
    } else {
      dry_radiation += record.solar_radiation_mj_per_m2;
      ++dry_days;
    }
  }

  ASSERT_GT(wet_days, 0);
  ASSERT_GT(dry_days, 0);
  EXPECT_LT(wet_radiation / wet_days, dry_radiation / dry_days);
}

TEST(SyntheticWeatherTest, DescribeAndTestConnectionReportTheSite) {
  const SyntheticWeatherSource source(test_site_parameters(), kSeed);

  EXPECT_EQ(source.describe().name, "synthetic:test_site");
  EXPECT_TRUE(source.test_connection().ok);
  EXPECT_NE(source.test_connection().message.find("test_site"), std::string::npos);
}

TEST(SyntheticWeatherTest, InvalidParametersAreReportedNotSimulated) {
  SyntheticWeatherParameters broken = test_site_parameters();
  broken.months[3].wet_day_probability = 1.5;
  const SyntheticWeatherSource source(broken, kSeed);

  const ConnectionStatus status = source.test_connection();
  EXPECT_FALSE(status.ok);
  EXPECT_NE(status.message.find("wet_day_probability"), std::string::npos);
  EXPECT_NE(status.message.find("month 4"), std::string::npos);
  EXPECT_THROW(static_cast<void>(source.fetch(DateRange::calendar_year(2023))),
               std::invalid_argument);
}

TEST(SyntheticWeatherTest, AnInvertedRangeIsAnError) {
  const SyntheticWeatherSource source(test_site_parameters(), kSeed);

  EXPECT_THROW(static_cast<void>(source.fetch(DateRange{Date{2023, 3, 1}, Date{2023, 1, 1}})),
               std::invalid_argument);
}

// The fingerprint is what a scenario bundle pins: two runs with the same seed
// but different climate normals must not look identical in the provenance.
TEST(SyntheticWeatherTest, TheFingerprintTracksTheParameters) {
  const SyntheticWeatherParameters parameters = test_site_parameters();
  SyntheticWeatherParameters changed = parameters;
  changed.months[0].mean_daily_max_c += 0.5;

  EXPECT_EQ(parameters.fingerprint(), test_site_parameters().fingerprint());
  EXPECT_NE(parameters.fingerprint(), changed.fingerprint());
  EXPECT_EQ(parameters.fingerprint().size(), 64U);
}

}  // namespace
}  // namespace paddock::core
