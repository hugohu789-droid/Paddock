// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#pragma once

#include <string>

#include <paddock/core/BudgetLedger.hpp>
#include <paddock/core/Weather.hpp>

namespace paddock::core {

/// Reference evapotranspiration by the temperature-based method of FAO-56
/// Eq. 52 (Hargreaves), in mm per day.
///
/// ETo = 0.0023 (Tmean + 17.8) (Tmax - Tmin)^0.5 Ra, with Ra expressed as
/// equivalent evaporation. FAO-56 recommends the Penman-Monteith equation
/// (Eq. 6) where humidity and wind are measured; Hargreaves is its documented
/// fallback when only temperature is reliable, which is the common case for the
/// CliFlo stations a farm is near. FAO-56 also says Eq. 52 should be checked
/// against Penman-Monteith in each new region - tracked in docs/verify.md.
[[nodiscard]] double hargreaves_reference_et_mm(double min_air_temperature_c,
                                                double max_air_temperature_c,
                                                double extraterrestrial_mj) noexcept;

/// Reference evapotranspiration for one day at one latitude, computing
/// extraterrestrial radiation from the date (FAO-56 Eq. 21).
[[nodiscard]] double reference_et_mm(const DailyWeather& weather, double latitude_degrees) noexcept;

/// Water stress coefficient (FAO-56 Eq. 84).
///
/// One where the root zone still holds readily available water, falling
/// linearly to zero at wilting point. This is the number that turns a dry
/// January into a feed deficit rather than just a lower soil moisture reading.
[[nodiscard]] double water_stress_coefficient(double depletion_mm, double total_available_water_mm,
                                              double depletion_fraction) noexcept;

/// Depletion fraction adjusted for evaporative demand (FAO-56 Table 22 note):
/// p = p_table + 0.04 (5 - ETc), clamped to [0.1, 0.8].
[[nodiscard]] double adjusted_depletion_fraction(double tabulated_fraction,
                                                 double crop_et_mm) noexcept;

/// Soil water parameters for one place. Every value comes from a soil or
/// management definition under data/, with its source cited there; nothing here
/// has a default that could pass for a measurement.
struct SoilWaterParameters {
  /// Total available water in the root zone, mm (FAO-56 Eq. 82).
  double total_available_water_mm = 0.0;
  /// Depletion fraction p before stress begins (FAO-56 Table 22).
  double depletion_fraction = 0.0;
  /// Crop coefficient Kc for the pasture (FAO-56 Table 12).
  double crop_coefficient = 0.0;
  /// Fraction of rainfall lost to surface runoff before it can infiltrate.
  double runoff_fraction = 0.0;

  /// TAW from measured soil properties (FAO-56 Eq. 82):
  /// 1000 (theta_FC - theta_WP) Zr, with volumetric water contents and a
  /// rooting depth in metres. S-map reports the first two per soil class.
  [[nodiscard]] static double total_available_water(double field_capacity_fraction,
                                                    double wilting_point_fraction,
                                                    double rooting_depth_m) noexcept;

  /// Empty when usable, otherwise an error naming the field.
  [[nodiscard]] std::string validation_error() const;
};

/// What one day did to the water budget, in mm. Returned rather than logged so
/// that the caller decides what to record and the bucket stays testable.
struct SoilWaterFluxes {
  double rainfall_mm = 0.0;
  double runoff_mm = 0.0;
  double infiltration_mm = 0.0;
  double reference_et_mm = 0.0;
  double evapotranspiration_mm = 0.0;  ///< Actual, after the crop and stress coefficients
  double drainage_mm = 0.0;
  double stress_coefficient = 1.0;
};

/// A single-layer soil water store: rainfall in, runoff, evapotranspiration and
/// drainage out.
///
/// One bucket per cell. It is the simplest thing that can produce a NZ pastoral
/// season - a wet winter that drains and leaches, a summer that empties the
/// profile and stops growth - and every flow it computes is reported to the
/// water budget, so the conservation gate covers it from the day it lands.
class SoilWaterBucket {
 public:
  /// `initial_water_mm` is the water held above wilting point, clamped into
  /// [0, TAW].
  SoilWaterBucket(SoilWaterParameters parameters, double initial_water_mm);

  /// Advances one day. When `ledger` is not null every flow is recorded there:
  /// rainfall as an inflow, runoff, evapotranspiration and drainage as
  /// outflows, all on the water line.
  SoilWaterFluxes step(const DailyWeather& weather, double latitude_degrees,
                       BudgetLedger* ledger = nullptr);

  /// Water held above wilting point, mm. This is the closing stock the
  /// conservation tests compare the ledger against.
  [[nodiscard]] double water_mm() const noexcept { return water_mm_; }

  /// Root zone depletion Dr, mm (FAO-56 Eq. 85): how far below field capacity.
  [[nodiscard]] double depletion_mm() const noexcept;

  /// Current stress coefficient, 1 when the pasture is not short of water.
  [[nodiscard]] double stress_coefficient() const noexcept;

  [[nodiscard]] const SoilWaterParameters& parameters() const noexcept { return parameters_; }

 private:
  SoilWaterParameters parameters_;
  double water_mm_ = 0.0;
};

}  // namespace paddock::core
