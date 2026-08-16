#include <cstddef>
#include <string>
#include <utility>

#include <paddock/core/Weather.hpp>

namespace paddock::core {

double DailyWeather::mean_air_temperature_c() const noexcept {
  return (min_air_temperature_c + max_air_temperature_c) / 2.0;
}

bool DailyWeather::is_valid() const noexcept {
  return date.is_valid() && rainfall_mm >= 0.0 && solar_radiation_mj_per_m2 >= 0.0 &&
         wind_speed_m_per_s >= 0.0 && max_air_temperature_c >= min_air_temperature_c;
}

DateRange DateRange::calendar_year(int year) noexcept {
  return DateRange{Date{year, 1, 1}, Date{year, 12, 31}};
}

std::int64_t DateRange::day_count() const noexcept {
  if (!is_valid()) {
    return 0;
  }
  return last.days_since_epoch() - first.days_since_epoch() + 1;
}

bool DateRange::contains(const Date& date) const noexcept {
  const std::int64_t day = date.days_since_epoch();
  return day >= first.days_since_epoch() && day <= last.days_since_epoch();
}

bool DateRange::is_valid() const noexcept {
  return first.is_valid() && last.is_valid() && first.days_since_epoch() <= last.days_since_epoch();
}

bool WeatherSeries::is_well_formed() const noexcept {
  for (std::size_t i = 0; i < records.size(); ++i) {
    if (!records[i].is_valid()) {
      return false;
    }
    if (i > 0 && records[i].date.days_since_epoch() != records[i - 1].date.days_since_epoch() + 1) {
      return false;
    }
  }
  return true;
}

double WeatherSeries::total_rainfall_mm() const noexcept {
  // Plain summation is enough here: a year of daily rainfall spans three orders
  // of magnitude at most, and nothing in the water budget is asserted from it.
  double total = 0.0;
  for (const DailyWeather& record : records) {
    total += record.rainfall_mm;
  }
  return total;
}

ConnectionStatus ConnectionStatus::available(std::string detail) {
  return ConnectionStatus{true, std::move(detail)};
}

ConnectionStatus ConnectionStatus::unavailable(std::string actionable_error) {
  return ConnectionStatus{false, std::move(actionable_error)};
}

}  // namespace paddock::core
