// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#pragma once

namespace paddock::core {

/// Solar geometry from FAO Irrigation and Drainage Paper 56 (Allen, Pereira,
/// Raes and Smith, 1998), Chapter 3.
///
/// These are astronomy, not agronomy: the numbers are fixed by the Earth's
/// orbit and are the same everywhere, which is why they are the one set of
/// constants that belongs in code rather than in a TOML file. Every one of them
/// carries its FAO-56 equation number so a reviewer can check it against the
/// source rather than against this comment.

/// Solar constant, MJ per square metre per minute (FAO-56 Eq. 21).
inline constexpr double kSolarConstantMjPerM2PerMinute = 0.0820;

/// Latent heat of vaporisation at about 20 C, MJ per kg (FAO-56 Eq. 20).
inline constexpr double kLatentHeatMjPerKg = 2.45;

/// The published rounding of 1 / kLatentHeatMjPerKg (FAO-56 Eq. 20). The
/// rounded figure is used rather than the exact reciprocal so that results
/// reproduce FAO-56's own worked examples digit for digit.
inline constexpr double kEvaporationMmPerMj = 0.408;

/// Radiation in MJ per square metre per day expressed as the depth of water it
/// could evaporate, mm per day (FAO-56 Eq. 20: 0.408 x radiation).
[[nodiscard]] double radiation_as_evaporation_mm(double radiation_mj_per_m2) noexcept;

/// Inverse relative Earth-Sun distance (FAO-56 Eq. 23).
[[nodiscard]] double inverse_relative_distance(int day_of_year) noexcept;

/// Solar declination in radians (FAO-56 Eq. 24).
[[nodiscard]] double solar_declination(int day_of_year) noexcept;

/// Sunset hour angle in radians (FAO-56 Eq. 25).
///
/// Inside the polar circles the argument to arccos leaves [-1, 1]; it is
/// clamped, which gives the physically right answer of a day with no sunset or
/// no sunrise. New Zealand never reaches that, but a raster tile from anywhere
/// else would.
[[nodiscard]] double sunset_hour_angle(double latitude_degrees, int day_of_year) noexcept;

/// Extraterrestrial radiation, MJ per square metre per day (FAO-56 Eq. 21).
///
/// This is the radiation arriving at the top of the atmosphere: the ceiling on
/// what any station can measure, and the driver of the temperature-based
/// evapotranspiration estimate used when a station reports no radiation.
[[nodiscard]] double extraterrestrial_radiation_mj(double latitude_degrees,
                                                   int day_of_year) noexcept;

/// Daylight hours (FAO-56 Eq. 34).
[[nodiscard]] double daylight_hours(double latitude_degrees, int day_of_year) noexcept;

/// Extraterrestrial radiation on a tilted surface, MJ per square metre per day.
///
/// FAO-56 stops at the horizontal case; the slope case is the subject of Allen,
/// Trezza and Tasumi (2006), "Analytical integrated functions for daily solar
/// radiation on slopes", Agricultural and Forest Meteorology 139:55-73 - the
/// same lead author, which is why it is the natural companion to the equations
/// above.
///
/// That paper integrates analytically, handling the awkward cases where a steep
/// slope facing away from the sun has more than one sunrise. This does the same
/// integral numerically instead, which is slower and much harder to get subtly
/// wrong: the geometry is a dot product between the surface normal and the sun
/// direction, both written out in Solar.cpp, with no case analysis to omit.
///
/// `aspect_degrees` is the compass bearing the ground faces, matching
/// Topography::aspect_degrees: 0 north, 90 east. It is ignored when
/// `slope_degrees` is zero, and **NaN is accepted there** - flat ground has no
/// aspect, and requiring a caller to substitute a fake one is how a fake one
/// ends up in the model.
///
/// Why this matters in New Zealand: on the sunny side of a hill the extra
/// radiation raises evapotranspiration, which is why Ballantrae's north- and
/// west-facing slopes run into seasonal water deficits despite over 1000 mm of
/// rain, and why measured production there is lower in summer and higher in
/// winter than on the shaded side. That reversal is a consequence to be
/// reproduced, not a coefficient to be applied.
[[nodiscard]] double extraterrestrial_radiation_on_slope_mj(double latitude_degrees,
                                                            int day_of_year, double slope_degrees,
                                                            double aspect_degrees) noexcept;

/// How much radiation a slope receives relative to level ground on the same day
/// at the same latitude, for transferring a measured horizontal figure onto a
/// slope.
///
/// One where the slope is level. Larger than one on the sunny side of a hill,
/// smaller on the shaded side. Zero when the slope is steep enough to be in
/// shadow all day, which is a real answer and not an error.
///
/// This is the ratio Allen et al. (2006) build their transfer on. Applying it
/// to a measured value assumes the sky is as clear on the slope as it was over
/// the instrument, which is the assumption every method of this kind makes and
/// the reason docs/verify.md carries it as a caveat.
[[nodiscard]] double slope_radiation_ratio(double latitude_degrees, int day_of_year,
                                           double slope_degrees, double aspect_degrees) noexcept;

}  // namespace paddock::core
