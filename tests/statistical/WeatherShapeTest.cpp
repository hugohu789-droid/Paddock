// The generator's statistical shape: wet-day frequency, rainfall depth, and the
// blending of monthly normals across a month boundary. Averaging noise away
// takes many simulated years, so these sit outside the pre-commit subset with
// the other statistical tests.

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>

#include <paddock/core/SimulationClock.hpp>
#include <paddock/core/SyntheticWeather.hpp>

#include "support/TestWeather.hpp"

namespace paddock::core {
namespace {

using test_support::test_site_parameters;

constexpr std::uint64_t kSeed = 20240701;

TEST(SyntheticWeatherTest, WetDayFrequencyAndDepthFollowTheParameters) {
  const SyntheticWeatherSource source(test_site_parameters(), kSeed);

  int wet_days = 0;
  int total_days = 0;
  double wet_depth = 0.0;
  for (int year = 2000; year < 2030; ++year) {
    for (const DailyWeather& record : source.fetch(DateRange::calendar_year(year)).records) {
      ++total_days;
      if (record.rainfall_mm > 0.0) {
        ++wet_days;
        wet_depth += record.rainfall_mm;
      }
    }
  }

  const double wet_fraction = static_cast<double>(wet_days) / total_days;
  EXPECT_NEAR(wet_fraction, 0.25, 0.01);
  EXPECT_NEAR(wet_depth / wet_days, 8.0, 0.5);
}

TEST(SyntheticWeatherTest, MonthlyNormalsAreBlendedRatherThanSteppedAcrossMonthEnds) {
  // Averaged over many years the noise cancels, so a step at a month boundary
  // would show up as a jump between consecutive days.
  const SyntheticWeatherSource source(test_site_parameters(), kSeed);
  double last_day_of_may = 0.0;
  double first_day_of_june = 0.0;
  constexpr int kYears = 300;

  for (int year = 1900; year < 1900 + kYears; ++year) {
    last_day_of_may += source.day(Date{year, 5, 31}).mean_air_temperature_c() / kYears;
    first_day_of_june += source.day(Date{year, 6, 1}).mean_air_temperature_c() / kYears;
  }

  // May's normal is 11 C and June's is 5.5 C in the fixture. A stepped
  // generator would show that whole 5.5 C gap between these two days; a blended
  // one moves by one day's worth of the gradient, about 0.2 C.
  EXPECT_LT(std::abs(last_day_of_may - first_day_of_june), 1.0);
}

}  // namespace
}  // namespace paddock::core
