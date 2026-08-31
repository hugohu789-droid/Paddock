// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#pragma once

#include <string>
#include <vector>

#include <paddock/core/Weather.hpp>

/// A pasture-borne mycotoxin: weather makes a fungus sporulate, stock eat the
/// spores, and the toxin damages the liver.
///
/// **This is the process, not the disease.** Facial eczema is
/// `data/diseases/facial-eczema.toml` - the thresholds, the toxin per spore and
/// the liver regression are parameters, so a second spore-borne toxin needs a
/// data file rather than a class. CLAUDE.md's rule about diseases being data is
/// the reason this header names none.
///
/// **Where this model is mechanism and where it is a fitted index**, because
/// the two must not be quoted the same way:
///
///   weather -> sporulation      cited (DairyNZ): grass minimum at or above
///                               12 C for four consecutive nights with rain
///   spores  -> toxin load       cited (Fitzgerald et al. 1998): 1.41 pg per
///                               spore, and 0.52 where strains are atoxigenic
///   load    -> GGT              **FITTED** to field thresholds
///   GGT     -> liver injury     cited (Morris et al. 2002)
///   GGT     -> milksolids       cited (Cuttance et al. 2021), dairy only
///
/// The middle step is fitted because the sourced numbers rule out doing it any
/// other way: at the spore count field guidance calls dangerous, accumulating
/// the single experimental dose that causes severe disease would take 283 days
/// against the 10 to 18 days guidance gives for clinical signs. The
/// explanations for that sixteen-fold gap are all unsourced, so none is
/// applied and the step is fitted and labelled instead. See
/// docs/validation/verify.md, open item 4.
namespace paddock::core {

/// Everything the model reads from the disease's data file.
struct MycotoxinParameters {
  // Sporulation.
  double grass_minimum_temperature_c = 12.0;
  int consecutive_nights = 4;
  double rainfall_mm_per_48h = 4.0;
  double rise_per_favourable_day = 1.9;
  double decay_per_unfavourable_day = 0.75;
  double background_spores_per_g = 2000.0;

  // Toxin.
  double picograms_per_spore = 1.41;

  // Response.
  double reactor_spore_days = 1'500'000.0;
  double reactor_ggt_iu_per_l = 55.0;
  double liver_injury_intercept = -2.96;
  double liver_injury_ln_ggt_slope = 0.89;
  double clinical_fraction_of_affected = 0.10;

  /// Rejects a parameter set that cannot produce a meaningful run, in the same
  /// shape as the other core parameter structs: a reason, or empty.
  [[nodiscard]] std::string invalid_reason() const;
};

/// Whether one day's weather favours sporulation.
///
/// `rain_previous_48h_mm` is the two-day total the source names, so a caller
/// has to carry yesterday's rain as well as today's.
[[nodiscard]] bool night_favours_sporulation(const DailyWeather& weather,
                                             double rain_previous_48h_mm,
                                             const MycotoxinParameters& parameters) noexcept;

/// Advances a spore count by one day.
///
/// Rises geometrically while the run of favourable nights is long enough, decays
/// geometrically otherwise, and never falls below the background the data file
/// names - the fungus lives on dead litter year round and a count of exactly
/// zero would be a claim nobody has made.
[[nodiscard]] double next_spore_count(double today_per_g, int consecutive_favourable_nights,
                                      const MycotoxinParameters& parameters) noexcept;

/// Toxin on the pasture, nanograms per gram, from the spore count.
[[nodiscard]] double toxin_ng_per_g(double spore_count_per_g,
                                    const MycotoxinParameters& parameters) noexcept;

/// **The fitted step.** Serum GGT from cumulative exposure in spore-days.
///
/// Exposure is spores per gram multiplied by days spent grazing them. The
/// reactor threshold is reached at `reactor_spore_days`, and GGT rises in
/// proportion beyond it. This is an empirical index calibrated so the published
/// field thresholds reproduce - it is not a toxin mass balance and must not be
/// reported as one.
[[nodiscard]] double ggt_from_exposure(double spore_days,
                                       const MycotoxinParameters& parameters) noexcept;

/// Liver injury score from serum GGT.
///
/// Morris, Smith and Hickey (2002): LIS = -2.96 + 0.89 ln(GGT), R2 = 0.54, and
/// the relationship held across sire lines selected for and against resistance.
/// Clamped at zero: the regression goes negative below about 27 IU/L, which is
/// a healthy animal rather than a liver in credit.
[[nodiscard]] double liver_injury_score(double ggt_iu_per_l,
                                        const MycotoxinParameters& parameters) noexcept;

/// How many of a mob would be showing signs a person could see.
///
/// About one in ten of the affected, per DairyNZ. The rest carry the same
/// damage without a visible sign, which is the fact this whole model exists to
/// make visible.
[[nodiscard]] int clinically_affected(int head, double ggt_iu_per_l,
                                      const MycotoxinParameters& parameters) noexcept;

/// The spore count on one piece of ground for every day of a weather series.
///
/// **This is where the disease meets the weather.** Rain is read as a rolling
/// 48-hour total because that is the window the source states, so the first day
/// of a series sees only its own rain and can never open an outbreak on its
/// own - which is right: an outbreak needs four nights, not one.
[[nodiscard]] std::vector<double> spore_count_series(const WeatherSeries& weather,
                                                     const MycotoxinParameters& parameters);

}  // namespace paddock::core
