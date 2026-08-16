#include <gtest/gtest.h>

#include <paddock/core/SimulationClock.hpp>
#include <paddock/core/Weather.hpp>

namespace paddock::core {
namespace {

DailyWeather winter_day() {
  DailyWeather weather;
  weather.date = Date{2023, 7, 1};
  weather.rainfall_mm = 4.5;
  weather.min_air_temperature_c = 2.1;
  weather.max_air_temperature_c = 11.9;
  weather.solar_radiation_mj_per_m2 = 6.2;
  weather.wind_speed_m_per_s = 3.1;
  return weather;
}

TEST(DailyWeatherTest, MeanTemperatureIsTheMidpointOfTheExtremes) {
  EXPECT_DOUBLE_EQ(winter_day().mean_air_temperature_c(), 7.0);
}

TEST(DailyWeatherTest, PhysicallyImpossibleRecordsAreRejected) {
  EXPECT_TRUE(winter_day().is_valid());

  DailyWeather inverted = winter_day();
  inverted.max_air_temperature_c = inverted.min_air_temperature_c - 1.0;
  EXPECT_FALSE(inverted.is_valid());

  DailyWeather negative_rain = winter_day();
  negative_rain.rainfall_mm = -0.1;
  EXPECT_FALSE(negative_rain.is_valid());

  DailyWeather impossible_date = winter_day();
  impossible_date.date = Date{2023, 2, 29};
  EXPECT_FALSE(impossible_date.is_valid());
}

TEST(DateRangeTest, ACalendarYearCountsItsDays) {
  EXPECT_EQ(DateRange::calendar_year(2023).day_count(), 365);
  EXPECT_EQ(DateRange::calendar_year(2024).day_count(), 366);
}

TEST(DateRangeTest, ContainmentIsInclusiveAtBothEnds) {
  const DateRange season{Date{2023, 7, 1}, Date{2024, 6, 30}};

  EXPECT_TRUE(season.contains(Date{2023, 7, 1}));
  EXPECT_TRUE(season.contains(Date{2024, 6, 30}));
  EXPECT_TRUE(season.contains(Date{2024, 2, 29}));
  EXPECT_FALSE(season.contains(Date{2023, 6, 30}));
  EXPECT_FALSE(season.contains(Date{2024, 7, 1}));
  EXPECT_EQ(season.day_count(), 366);
}

TEST(DateRangeTest, InvertedRangesAreInvalidAndCountNothing) {
  const DateRange inverted{Date{2024, 1, 10}, Date{2024, 1, 1}};

  EXPECT_FALSE(inverted.is_valid());
  EXPECT_EQ(inverted.day_count(), 0);
}

TEST(WeatherSeriesTest, WellFormedRequiresContiguousAscendingValidDays) {
  WeatherSeries series;
  series.records.push_back(winter_day());
  DailyWeather second = winter_day();
  second.date = Date{2023, 7, 2};
  series.records.push_back(second);

  EXPECT_TRUE(series.is_well_formed());
  EXPECT_EQ(series.size(), 2U);
  EXPECT_DOUBLE_EQ(series.total_rainfall_mm(), 9.0);

  // A gap is the failure that matters: to the soil water bucket a missing day
  // is indistinguishable from a dry one.
  WeatherSeries with_gap = series;
  with_gap.records.back().date = Date{2023, 7, 3};
  EXPECT_FALSE(with_gap.is_well_formed());

  WeatherSeries out_of_order = series;
  out_of_order.records.back().date = Date{2023, 6, 30};
  EXPECT_FALSE(out_of_order.is_well_formed());
}

TEST(ConnectionStatusTest, CarriesAnActionableMessageEitherWay) {
  const ConnectionStatus ready = ConnectionStatus::available("1096 days loaded");
  const ConnectionStatus missing =
      ConnectionStatus::unavailable("snapshot not found; run scripts/cliflo-snapshot.py");

  EXPECT_TRUE(ready.ok);
  EXPECT_EQ(ready.message, "1096 days loaded");
  EXPECT_FALSE(missing.ok);
  EXPECT_NE(missing.message.find("cliflo-snapshot"), std::string::npos);
}

}  // namespace
}  // namespace paddock::core
