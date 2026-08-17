// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

#include <paddock/core/SoilWater.hpp>
#include <paddock/core/Solar.hpp>

namespace paddock::core {

namespace {

/// FAO-56 Eq. 52 coefficients.
constexpr double kHargreavesCoefficient = 0.0023;
constexpr double kHargreavesTemperatureOffset = 17.8;

/// FAO-56 Table 22 note: the adjustment of p for evaporative demand.
constexpr double kDepletionAdjustmentSlope = 0.04;
constexpr double kDepletionAdjustmentReferenceEt = 5.0;
constexpr double kMinimumDepletionFraction = 0.1;
constexpr double kMaximumDepletionFraction = 0.8;

}  // namespace

double hargreaves_reference_et_mm(double min_air_temperature_c, double max_air_temperature_c,
                                  double extraterrestrial_mj) noexcept {
  const double range = max_air_temperature_c - min_air_temperature_c;
  if (range <= 0.0 || extraterrestrial_mj <= 0.0) {
    return 0.0;
  }
  const double mean = (min_air_temperature_c + max_air_temperature_c) / 2.0;
  const double radiation_mm = radiation_as_evaporation_mm(extraterrestrial_mj);
  const double estimate = kHargreavesCoefficient * (mean + kHargreavesTemperatureOffset) *
                          std::sqrt(range) * radiation_mm;
  // Below -17.8 C the formula turns negative. Air that cold does not drive
  // water back into the soil, so the estimate is floored rather than trusted.
  return std::max(0.0, estimate);
}

double reference_et_mm(const DailyWeather& weather, double latitude_degrees) noexcept {
  return hargreaves_reference_et_mm(
      weather.min_air_temperature_c, weather.max_air_temperature_c,
      extraterrestrial_radiation_mj(latitude_degrees, weather.date.day_of_year()));
}

double water_stress_coefficient(double depletion_mm, double total_available_water_mm,
                                double depletion_fraction) noexcept {
  if (total_available_water_mm <= 0.0) {
    return 0.0;
  }
  const double readily_available = depletion_fraction * total_available_water_mm;
  if (depletion_mm <= readily_available) {
    return 1.0;
  }
  const double remaining = total_available_water_mm - readily_available;
  if (remaining <= 0.0) {
    return 0.0;
  }
  return std::clamp((total_available_water_mm - depletion_mm) / remaining, 0.0, 1.0);
}

double adjusted_depletion_fraction(double tabulated_fraction, double crop_et_mm) noexcept {
  const double adjusted = tabulated_fraction + (kDepletionAdjustmentSlope *
                                                (kDepletionAdjustmentReferenceEt - crop_et_mm));
  return std::clamp(adjusted, kMinimumDepletionFraction, kMaximumDepletionFraction);
}

double SoilWaterParameters::total_available_water(double field_capacity_fraction,
                                                  double wilting_point_fraction,
                                                  double rooting_depth_m) noexcept {
  return 1000.0 * (field_capacity_fraction - wilting_point_fraction) * rooting_depth_m;
}

std::string SoilWaterParameters::validation_error() const {
  if (total_available_water_mm <= 0.0) {
    return "total_available_water_mm must be positive";
  }
  if (depletion_fraction <= 0.0 || depletion_fraction >= 1.0) {
    return "depletion_fraction must be between 0 and 1";
  }
  if (crop_coefficient <= 0.0) {
    return "crop_coefficient must be positive";
  }
  if (runoff_fraction < 0.0 || runoff_fraction >= 1.0) {
    return "runoff_fraction must be between 0 and 1";
  }
  return {};
}

SoilWaterBucket::SoilWaterBucket(SoilWaterParameters parameters, double initial_water_mm)
    : parameters_(parameters) {
  const std::string error = parameters_.validation_error();
  if (!error.empty()) {
    throw std::invalid_argument("SoilWaterBucket: " + error);
  }
  water_mm_ = std::clamp(initial_water_mm, 0.0, parameters_.total_available_water_mm);
}

double SoilWaterBucket::depletion_mm() const noexcept {
  return parameters_.total_available_water_mm - water_mm_;
}

double SoilWaterBucket::stress_coefficient() const noexcept {
  return water_stress_coefficient(depletion_mm(), parameters_.total_available_water_mm,
                                  parameters_.depletion_fraction);
}

SoilWaterFluxes SoilWaterBucket::step(const DailyWeather& weather, double latitude_degrees,
                                      BudgetLedger* ledger) {
  SoilWaterFluxes fluxes;
  fluxes.rainfall_mm = std::max(0.0, weather.rainfall_mm);
  fluxes.runoff_mm = fluxes.rainfall_mm * parameters_.runoff_fraction;
  fluxes.infiltration_mm = fluxes.rainfall_mm - fluxes.runoff_mm;

  fluxes.reference_et_mm = reference_et_mm(weather, latitude_degrees);
  const double crop_et = parameters_.crop_coefficient * fluxes.reference_et_mm;

  // Stress is judged on the profile as it stood this morning, and the depletion
  // fraction moves with demand (FAO-56 Table 22 note): a hot day dries the
  // readily available water sooner than a mild one.
  const double fraction = adjusted_depletion_fraction(parameters_.depletion_fraction, crop_et);
  fluxes.stress_coefficient =
      water_stress_coefficient(depletion_mm(), parameters_.total_available_water_mm, fraction);

  // The profile cannot give up more water than it holds, whatever the demand.
  const double demand = crop_et * fluxes.stress_coefficient;
  fluxes.evapotranspiration_mm = std::min(demand, water_mm_ + fluxes.infiltration_mm);

  const double after_flows = water_mm_ + fluxes.infiltration_mm - fluxes.evapotranspiration_mm;
  fluxes.drainage_mm = std::max(0.0, after_flows - parameters_.total_available_water_mm);

  // Derived from the flows rather than recomputed, so that the closing stock
  // and the ledger cannot disagree by construction.
  water_mm_ = after_flows - fluxes.drainage_mm;

  if (ledger != nullptr) {
    ledger->record_inflow(Budget::Water, "rainfall", fluxes.rainfall_mm);
    ledger->record_outflow(Budget::Water, "runoff", fluxes.runoff_mm);
    ledger->record_outflow(Budget::Water, "evapotranspiration", fluxes.evapotranspiration_mm);
    ledger->record_outflow(Budget::Water, "drainage", fluxes.drainage_mm);
  }

  return fluxes;
}

}  // namespace paddock::core
