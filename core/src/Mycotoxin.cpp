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
  if (carrying_capacity_spores_per_g <= background_spores_per_g) {
    return "carrying_capacity_spores_per_g must exceed the background it rises from";
  }
  if (background_toxin_ng_days_per_year < 0.0) {
    return "background_toxin_ng_days_per_year cannot be negative";
  }
  if (clearance_per_day < 0.0 || clearance_per_day >= 1.0) {
    return "clearance_per_day is a daily share and must lie in [0, 1)";
  }
  if (picograms_per_spore <= 0.0) {
    return "picograms_per_spore must be positive";
  }
  if (reactor_toxin_ng_days <= 0.0) {
    return "reactor_toxin_ng_days must be positive";
  }
  if (reactor_ggt_iu_per_l <= 0.0) {
    return "reactor_ggt_iu_per_l must be positive";
  }
  if (full_dose_spores_per_g < monitor_spores_per_g) {
    return "full_dose_spores_per_g cannot be below the count that starts monitoring";
  }
  if (stand_down_spores_per_g > monitor_spores_per_g) {
    return "stand_down_spores_per_g must sit at or below the monitoring count";
  }
  if (stand_down_weeks < 1) {
    return "stand_down_weeks must be at least one";
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
    // Bounded, because litter is. An unbounded rise turns a long spell into a
    // number with no physical meaning - see carrying_capacity_spores_per_g.
    return std::min(from * parameters.rise_per_favourable_day,
                    parameters.carrying_capacity_spores_per_g);
  }
  return std::max(floor_count, from * parameters.decay_per_unfavourable_day);
}

double toxin_ng_per_g(double spore_count_per_g, const MycotoxinParameters& parameters) noexcept {
  // Picograms per spore to nanograms per gram: a thousand picograms in a
  // nanogram, and the count is already per gram of pasture.
  return std::max(0.0, spore_count_per_g) * parameters.picograms_per_spore / 1000.0;
}

double next_exposure(double carried_toxin_ng_days, double today_toxin_ng_per_g,
                     const MycotoxinParameters& parameters) noexcept {
  // The background a clean year delivers, spread over the year. Subtracting it
  // daily rather than at the end means a run of any length behaves the same,
  // which a model that can be asked for three years has to do.
  const double background_today =
      std::max(0.0, parameters.background_toxin_ng_days_per_year) / 365.0;
  const double charged = std::max(0.0, today_toxin_ng_per_g - background_today);

  const double clearance = std::clamp(parameters.clearance_per_day, 0.0, 1.0);
  return std::max(0.0, (carried_toxin_ng_days * (1.0 - clearance)) + charged);
}

double ggt_from_exposure(double toxin_ng_days, const MycotoxinParameters& parameters) noexcept {
  if (toxin_ng_days <= 0.0) {
    return 0.0;
  }
  // Linear in exposure through the reactor point. Deliberately the simplest
  // shape that reproduces the field thresholds: a curve with more parameters
  // would fit them no better and would look like it knew more than it does.
  return parameters.reactor_ggt_iu_per_l * (toxin_ng_days / parameters.reactor_toxin_ng_days);
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

std::vector<MycotoxinYear> mycotoxin_years(const WeatherSeries& weather,
                                           double monitoring_spores_per_g,
                                           double dangerous_spores_per_g,
                                           const MycotoxinParameters& parameters) {
  const std::vector<double> counts = spore_count_series(weather, parameters);

  std::vector<MycotoxinYear> years;
  double carried = 0.0;

  // **The published programme, replayed.** DairyNZ start it at the full dose
  // when farm counts reach 30,000 and end it only after three weeks at or below
  // 10,000 - the stand-down is the part a model must not shorten, because it is
  // what stops a farmer walking away from a season that is not over.
  bool on_zinc = false;
  int days_below_stand_down = 0;

  for (std::size_t day = 0; day < counts.size() && day < weather.records.size(); ++day) {
    const Date& date = weather.records[day].date;

    // July opens the farm year, so anything before July belongs to the year
    // that started the previous July.
    const int starting_year = date.month >= 7 ? date.year : date.year - 1;
    if (years.empty() || years.back().starting_year != starting_year) {
      MycotoxinYear opened;
      opened.starting_year = starting_year;
      years.push_back(opened);
    }

    // Carried across the boundary on purpose: a liver does not reset in July.
    carried = next_exposure(carried, toxin_ng_per_g(counts[day], parameters), parameters);

    MycotoxinYear& year = years.back();
    year.peak_spores_per_g = std::max(year.peak_spores_per_g, counts[day]);
    year.peak_ggt_iu_per_l =
        std::max(year.peak_ggt_iu_per_l, ggt_from_exposure(carried, parameters));
    if (counts[day] >= monitoring_spores_per_g) {
      ++year.days_at_or_above_monitoring;
    }
    if (counts[day] >= dangerous_spores_per_g) {
      ++year.days_at_or_above_dangerous;
    }

    if (!on_zinc) {
      if (counts[day] >= parameters.full_dose_spores_per_g) {
        on_zinc = true;
        days_below_stand_down = 0;
        if (!year.started_this_year) {
          year.started_this_year = true;
          year.programme_started = date;
        }
      }
    } else {
      days_below_stand_down =
          counts[day] <= parameters.stand_down_spores_per_g ? days_below_stand_down + 1 : 0;
      if (days_below_stand_down >= parameters.stand_down_weeks * 7) {
        on_zinc = false;
      }
    }

    if (on_zinc) {
      ++year.zinc_programme_days;
    }
  }
  return years;
}

}  // namespace paddock::core
