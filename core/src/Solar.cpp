#include <algorithm>
#include <cmath>

#include <paddock/core/Solar.hpp>

namespace paddock::core {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kMinutesPerDay = 24.0 * 60.0;
constexpr double kDaysPerYear = 365.0;

double to_radians(double degrees) noexcept {
  return (kPi / 180.0) * degrees;
}

}  // namespace

double radiation_as_evaporation_mm(double radiation_mj_per_m2) noexcept {
  return kEvaporationMmPerMj * radiation_mj_per_m2;
}

double inverse_relative_distance(int day_of_year) noexcept {
  return 1.0 + (0.033 * std::cos(((2.0 * kPi) / kDaysPerYear) * day_of_year));
}

double solar_declination(int day_of_year) noexcept {
  return 0.409 * std::sin((((2.0 * kPi) / kDaysPerYear) * day_of_year) - 1.39);
}

double sunset_hour_angle(double latitude_degrees, int day_of_year) noexcept {
  const double latitude = to_radians(latitude_degrees);
  const double declination = solar_declination(day_of_year);
  const double cosine = -std::tan(latitude) * std::tan(declination);
  return std::acos(std::clamp(cosine, -1.0, 1.0));
}

double extraterrestrial_radiation_mj(double latitude_degrees, int day_of_year) noexcept {
  const double latitude = to_radians(latitude_degrees);
  const double declination = solar_declination(day_of_year);
  const double hour_angle = sunset_hour_angle(latitude_degrees, day_of_year);

  return (kMinutesPerDay / kPi) * kSolarConstantMjPerM2PerMinute *
         inverse_relative_distance(day_of_year) *
         ((hour_angle * std::sin(latitude) * std::sin(declination)) +
          (std::cos(latitude) * std::cos(declination) * std::sin(hour_angle)));
}

double daylight_hours(double latitude_degrees, int day_of_year) noexcept {
  return (24.0 / kPi) * sunset_hour_angle(latitude_degrees, day_of_year);
}

}  // namespace paddock::core
