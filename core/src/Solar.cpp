// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

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

namespace {

/// Integration steps between sunrise and sunset.
///
/// The integral has no closed form once a slope can shade itself, so it is
/// summed with the midpoint rule over the daylight interval - taken from the
/// analytic sunset hour angle rather than over the whole 24 hours, which would
/// put the integrand's kink at sunrise inside the range and cost an order of
/// convergence.
///
/// The count was chosen by measuring, not by looking generous. Worst
/// disagreement with FAO-56 Eq. 21 over a year at 43.6 S, level ground:
///
///     steps      error (MJ)
///       288       1.881e-04
///       576       4.702e-05
///      1440       7.523e-06
///      5760       4.702e-07
///
/// Clean second order. 288 is kept because 1.9e-4 MJ is five parts per million
/// of a summer day's radiation, and the measured radiation this will be applied
/// to carries an error of twenty to thirty per cent. Twenty times the work to
/// make a test tolerance look tighter would buy nothing real.
constexpr int kIntegrationSteps = 288;

/// Radiation received over one day by a surface whose normal is `normal`, in the
/// local east-north-up frame, in MJ per square metre.
///
/// The integrand is the dot product of the surface normal with the direction of
/// the sun, clamped at zero: a surface receives nothing while the sun is behind
/// it, and nothing at all while the sun is below the horizon. Writing it this
/// way means there is no case analysis to get wrong - the awkward situations
/// Allen et al. (2006) enumerate analytically, such as a steep shaded slope
/// with two sunrises, fall out of the clamp.
double integrate_daily_radiation(double latitude_radians, int day_of_year, double normal_east,
                                 double normal_north, double normal_up) noexcept {
  const double declination = solar_declination(day_of_year);
  const double sin_latitude = std::sin(latitude_radians);
  const double cos_latitude = std::cos(latitude_radians);
  const double sin_declination = std::sin(declination);
  const double cos_declination = std::cos(declination);

  // Sunrise and sunset for level ground at this latitude and date. Outside that
  // window nothing receives anything, whatever it faces.
  const double half_day =
      std::acos(std::clamp(-std::tan(latitude_radians) * std::tan(declination), -1.0, 1.0));
  if (!(half_day > 0.0)) {
    return 0.0;
  }

  const double step = (2.0 * half_day) / static_cast<double>(kIntegrationSteps);
  double total = 0.0;

  for (int i = 0; i < kIntegrationSteps; ++i) {
    // Hour angle: zero at solar noon, negative before it. Sampled at the middle
    // of each step, which is the midpoint rule and keeps the level case
    // symmetric about noon.
    const double hour_angle = -half_day + ((static_cast<double>(i) + 0.5) * step);
    const double cos_hour = std::cos(hour_angle);
    const double sin_hour = std::sin(hour_angle);

    // Direction of the sun in the east-north-up frame. The vertical component
    // is the sine of the solar altitude, so it is also the test for whether the
    // sun is up at all.
    const double sun_up =
        (sin_latitude * sin_declination) + (cos_latitude * cos_declination * cos_hour);
    if (sun_up <= 0.0) {
      continue;
    }
    const double sun_east = -cos_declination * sin_hour;
    const double sun_north =
        (sin_declination * cos_latitude) - (cos_declination * sin_latitude * cos_hour);

    const double cos_incidence =
        (normal_east * sun_east) + (normal_north * sun_north) + (normal_up * sun_up);
    if (cos_incidence > 0.0) {
      total += cos_incidence * step;
    }
  }

  // The same constant FAO-56 Eq. 21 carries, halved because that equation folds
  // a symmetric half-day integral into one term and this sums both halves.
  return (kMinutesPerDay / (2.0 * kPi)) * kSolarConstantMjPerM2PerMinute *
         inverse_relative_distance(day_of_year) * total;
}

}  // namespace

double extraterrestrial_radiation_on_slope_mj(double latitude_degrees, int day_of_year,
                                              double slope_degrees,
                                              double aspect_degrees) noexcept {
  const double slope = to_radians(slope_degrees);

  // Level ground has no aspect, and callers are allowed to say so with NaN.
  // Multiplying NaN into the normal would poison the whole integral, so the
  // level case is answered before the aspect is looked at.
  if (!(std::abs(slope) > 0.0)) {
    return integrate_daily_radiation(to_radians(latitude_degrees), day_of_year, 0.0, 0.0, 1.0);
  }

  const double aspect = to_radians(aspect_degrees);
  // Normal of ground tilted by `slope` towards the compass bearing `aspect`.
  // Bearing is clockwise from north, so its east component is the sine and its
  // north component the cosine - the opposite of the mathematical convention,
  // and the reason this is written out rather than assumed.
  const double normal_east = std::sin(slope) * std::sin(aspect);
  const double normal_north = std::sin(slope) * std::cos(aspect);
  const double normal_up = std::cos(slope);

  return integrate_daily_radiation(to_radians(latitude_degrees), day_of_year, normal_east,
                                   normal_north, normal_up);
}

double slope_radiation_ratio(double latitude_degrees, int day_of_year, double slope_degrees,
                             double aspect_degrees) noexcept {
  const double level =
      extraterrestrial_radiation_on_slope_mj(latitude_degrees, day_of_year, 0.0, 0.0);
  if (!(level > 0.0)) {
    // Polar night: no radiation anywhere, so no ratio to report. One leaves a
    // caller's measured value unchanged, which is the harmless answer.
    return 1.0;
  }
  return extraterrestrial_radiation_on_slope_mj(latitude_degrees, day_of_year, slope_degrees,
                                                aspect_degrees) /
         level;
}

}  // namespace paddock::core
