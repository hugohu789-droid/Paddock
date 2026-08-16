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

}  // namespace paddock::core
