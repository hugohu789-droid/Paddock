// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include <paddock/core/Mycotoxin.hpp>

namespace paddock::core {

std::string MycotoxinParameters::invalid_reason() const {
  if (consecutive_nights < 1) {
    return "consecutive_nights must be at least one";
  }
  if (rainfall_mm_per_48h < 0.0) {
    return "rainfall_mm_per_48h cannot be negative";
  }
  if (rise_per_favourable_day <= 1.0) {
    return "rise_per_favourable_day must exceed one or a count can never rise";
  }
  if (decay_per_unfavourable_day <= 0.0 || decay_per_unfavourable_day >= 1.0) {
    return "decay_per_unfavourable_day must lie between zero and one";
  }
  if (background_spores_per_g < 0.0) {
    return "background_spores_per_g cannot be negative";
  }
  if (picograms_per_spore <= 0.0) {
    return "picograms_per_spore must be positive";
  }
  if (reactor_spore_days <= 0.0) {
    return "reactor_spore_days must be positive";
  }
  if (reactor_ggt_iu_per_l <= 0.0) {
    return "reactor_ggt_iu_per_l must be positive";
  }
  if (clinical_fraction_of_affected < 0.0 || clinical_fraction_of_affected > 1.0) {
    return "clinical_fraction_of_affected is a share and must lie in [0, 1]";
  }
  return {};
}

bool night_favours_sporulation(const DailyWeather& weather, double rain_previous_48h_mm,
                               const MycotoxinParameters& parameters) noexcept {
  // **Grass minimum, not air minimum.** The source says grass, and the two
  // differ: a grass-level thermometer radiates to the sky and reads below the
  // screen minimum, which is what a ground frost is. So grass minimum sits at
  // or under air minimum, and testing the air minimum against a grass
  // threshold **accepts nights the source would reject** - it over-triggers
  // rather than under-triggers.
  //
  // It is least wrong exactly where it matters. Radiative cooling is what
  // opens the gap, and it is suppressed under the damp overcast skies that the
  // rainfall half of this test is selecting for, so on the nights that pass
  // both conditions the two readings are closest. Recorded in
  // docs/validation/verify.md rather than closed with a correction nobody has
  // published.
  const bool warm = weather.min_air_temperature_c >= parameters.grass_minimum_temperature_c;
  const bool damp = rain_previous_48h_mm >= parameters.rainfall_mm_per_48h;
  return warm && damp;
}

double next_spore_count(double today_per_g, int consecutive_favourable_nights,
                        const MycotoxinParameters& parameters) noexcept {
  const double floor_count = std::max(0.0, parameters.background_spores_per_g);
  const double from = std::max(today_per_g, floor_count);

  // The run has to be long enough before anything happens. That is what makes
  // an outbreak the "major rapid rise" the source describes rather than a slope
  // that starts on the first warm night.
  if (consecutive_favourable_nights >= parameters.consecutive_nights) {
    return from * parameters.rise_per_favourable_day;
  }
  return std::max(floor_count, from * parameters.decay_per_unfavourable_day);
}

double toxin_ng_per_g(double spore_count_per_g, const MycotoxinParameters& parameters) noexcept {
  // Picograms per spore to nanograms per gram: a thousand picograms in a
  // nanogram, and the count is already per gram of pasture.
  return std::max(0.0, spore_count_per_g) * parameters.picograms_per_spore / 1000.0;
}

double ggt_from_exposure(double spore_days, const MycotoxinParameters& parameters) noexcept {
  if (spore_days <= 0.0) {
    return 0.0;
  }
  // Linear in exposure through the reactor point. Deliberately the simplest
  // shape that reproduces the field thresholds: a curve with more parameters
  // would fit them no better and would look like it knew more than it does.
  return parameters.reactor_ggt_iu_per_l * (spore_days / parameters.reactor_spore_days);
}

double liver_injury_score(double ggt_iu_per_l, const MycotoxinParameters& parameters) noexcept {
  if (ggt_iu_per_l <= 0.0) {
    return 0.0;
  }
  const double score = parameters.liver_injury_intercept +
                       (parameters.liver_injury_ln_ggt_slope * std::log(ggt_iu_per_l));
  return std::max(0.0, score);
}

int clinically_affected(int head, double ggt_iu_per_l,
                        const MycotoxinParameters& parameters) noexcept {
  if (head <= 0 || ggt_iu_per_l < parameters.reactor_ggt_iu_per_l) {
    return 0;
  }
  // **Rounded down on purpose.** Reporting "0.4 animals showing signs" is worse
  // than reporting none: a mob under the threshold for one visible case is a
  // mob where a farmer would see nothing, which is the point being made.
  const double affected = static_cast<double>(head) * parameters.clinical_fraction_of_affected;
  return static_cast<int>(std::floor(affected));
}

std::vector<double> spore_count_series(const WeatherSeries& weather,
                                       const MycotoxinParameters& parameters) {
  std::vector<double> counts;
  counts.reserve(weather.records.size());

  double count = std::max(0.0, parameters.background_spores_per_g);
  int run = 0;
  double yesterday_rain_mm = 0.0;

  for (const DailyWeather& day : weather.records) {
    const double rain_48h = day.rainfall_mm + yesterday_rain_mm;
    run = night_favours_sporulation(day, rain_48h, parameters) ? run + 1 : 0;
    count = next_spore_count(count, run, parameters);
    counts.push_back(count);
    yesterday_rain_mm = day.rainfall_mm;
  }
  return counts;
}

}  // namespace paddock::core
