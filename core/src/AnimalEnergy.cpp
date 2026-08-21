// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

#include <paddock/core/AnimalEnergy.hpp>

namespace paddock::core {

std::string to_string(AnimalKind kind) {
  switch (kind) {
    case AnimalKind::Sheep:
      return "sheep";
    case AnimalKind::Cattle:
      return "cattle";
    case AnimalKind::Deer:
      return "deer";
    case AnimalKind::Other:
      break;
  }
  return "other";
}

AnimalKind animal_kind_from(const std::string& name) {
  if (name == "sheep") {
    return AnimalKind::Sheep;
  }
  if (name == "cattle") {
    return AnimalKind::Cattle;
  }
  if (name == "deer") {
    return AnimalKind::Deer;
  }
  return AnimalKind::Other;
}

namespace {

/// TMC Eq. 13: the base rate, MJ per kg of metabolic liveweight.
constexpr double kBasalMjPerKgMetabolic = 0.28;

/// TMC Eq. 17.
constexpr double kAgeDecayPerDay = 0.00008;
constexpr double kAgeFactorFloor = 0.84;

/// TMC Eq. 21 and 24.
constexpr double kMovementMjPerKgPerKm = 0.0026;
constexpr double kClimbMjPerKgPerKm = 0.028;

/// TMC Eq. 22.
constexpr double kPastureMassSlope = 0.000057;
constexpr double kPastureMassIntercept = 0.16;

/// TMC Eq. 18: the digestibility term, (0.9 - digest/100).
constexpr double kChewDigestibilityOffset = 0.9;

/// TMC Eq. 44.
constexpr double kGainFloorMjPerKg = 6.7;
constexpr double kGainSigmoidSteepness = 6.0;
constexpr double kGainSigmoidMidpoint = 0.4;

/// TMC Eq. 46.
constexpr double kGainRateDivisor = 4.0;

/// TMC Eq. 5 and 6.
constexpr double kMilkMaintenanceEfficiency = 0.85;
constexpr double kMaintenanceEfficiencySlope = 0.35;
constexpr double kMaintenanceEfficiencyIntercept = 0.503;

/// TMC Eq. 9 and 8.
constexpr double kGainEfficiencySlope = 0.042;
constexpr double kGainEfficiencyIntercept = 0.006;
constexpr double kMilkGainEfficiency = 0.7;

/// TMC Eq. 8: the divisor that makes mobilised tissue more efficient than
/// deposited tissue.
constexpr double kLossEfficiencyDivisor = 0.8;

/// TMC Eq. 52 and section 5.2.2.
constexpr double kInitialChewFractionOfBasal = 0.046;
constexpr double kMaintenanceToleranceMj = 0.1;
constexpr int kMaximumIterations = 5;

/// TMC Eq. 54: the share of production cost charged to maintenance.
constexpr double kProductionMaintenanceShare = 0.1;

constexpr double kDegreesToRadians = 3.14159265358979323846 / 180.0;

/// Metabolic liveweight, lwt^0.75. Every published maintenance equation since
/// Kleiber uses it, and it appears in TMC Eq. 13 and Eq. 46 alike.
double metabolic_weight(double liveweight_kg) noexcept {
  return std::pow(liveweight_kg, 0.75);
}

}  // namespace

double DietQuality::energy_density() const noexcept {
  return metabolisable_energy_mj_per_kg_dm / kGrossEnergyMjPerKgDm;
}

double DietQuality::maintenance_efficiency() const noexcept {
  // TMC Eq. 5 applies to milk diets, Eq. 6 to everything else. A diet that is
  // part milk is not covered by either as written; the blend below is the
  // obvious reading and is flagged in docs/validation/verify.md rather than presented as
  // the manual's.
  const double forage =
      (kMaintenanceEfficiencySlope * energy_density()) + kMaintenanceEfficiencyIntercept;
  return (milk_fraction * kMilkMaintenanceEfficiency) + ((1.0 - milk_fraction) * forage);
}

double DietQuality::gain_efficiency() const noexcept {
  // TMC Eq. 9 for the forage part, blended with milk at 0.7 by Eq. 8.
  const double forage =
      (kGainEfficiencySlope * metabolisable_energy_mj_per_kg_dm) + kGainEfficiencyIntercept;
  return (forage * (1.0 - milk_fraction)) + (kMilkGainEfficiency * milk_fraction);
}

double DietQuality::loss_efficiency() const noexcept {
  return maintenance_efficiency() / kLossEfficiencyDivisor;
}

std::string DietQuality::validation_error() const {
  if (metabolisable_energy_mj_per_kg_dm <= 0.0) {
    return "diet metabolisable energy must be positive";
  }
  // Feed cannot carry more metabolisable energy than it carries gross energy.
  if (metabolisable_energy_mj_per_kg_dm > kGrossEnergyMjPerKgDm) {
    return "diet metabolisable energy of " + std::to_string(metabolisable_energy_mj_per_kg_dm) +
           " MJ/kg DM exceeds the gross energy of feed, 18.4 MJ/kg DM";
  }
  if (digestibility_percent < 0.0 || digestibility_percent > 100.0) {
    return "digestibility must be a percentage between 0 and 100";
  }
  if (milk_fraction < 0.0 || milk_fraction > 1.0) {
    return "the milk fraction of the diet must be between 0 and 1";
  }
  return {};
}

std::string AnimalClassParameters::validation_error() const {
  if (class_id.empty()) {
    return "an animal class needs an identifier";
  }
  if (species_factor <= 0.0) {
    return class_id + ": the species factor K must be positive";
  }
  if (sex_factor <= 0.0) {
    return class_id + ": the sex factor S must be positive";
  }
  if (standard_reference_weight_kg <= 0.0) {
    return class_id + ": the standard reference weight must be positive";
  }
  if (grazing_coefficient < 0.0) {
    return class_id + ": the grazing coefficient must not be negative";
  }
  if (gain_energy_ceiling_mj_per_kg <= 0.0) {
    return class_id + ": the energy ceiling for gain must be positive";
  }
  return {};
}

double age_factor(double age_days) noexcept {
  return std::max(kAgeFactorFloor, std::exp(-kAgeDecayPerDay * age_days));
}

double basal_net_energy_mj(const AnimalClassParameters& animal, const AnimalState& state) noexcept {
  // M is 1: it applies only before weaning, and Nicol and Brookes omit it.
  return kBasalMjPerKgMetabolic * animal.species_factor * animal.sex_factor *
         age_factor(state.age_days) * metabolic_weight(state.liveweight_kg);
}

double slope_movement_factor(double slope_degrees) noexcept {
  return 1.0 + std::tan(slope_degrees * kDegreesToRadians);
}

double movement_net_energy_mj(const AnimalState& state, const GrazingConditions& ground) noexcept {
  // TMC Eq. 22, with pasture mass in t DM/ha converted to kg as the equation
  // expects: fmove = 0.000057 * (t * 1000) + 0.16.
  const double fmove =
      (kPastureMassSlope * ground.pasture_mass_t_dm_per_ha * 1000.0) + kPastureMassIntercept;
  if (fmove <= 0.0) {
    return 0.0;
  }
  return kMovementMjPerKgPerKm * state.liveweight_kg * slope_movement_factor(ground.slope_degrees) *
         ground.area_per_animal_ha / fmove;
}

double activity_net_energy_mj(const AnimalState& state, const GrazingConditions& ground) noexcept {
  return state.liveweight_kg * ((kMovementMjPerKgPerKm * ground.horizontal_km_per_day) +
                                (kClimbMjPerKgPerKm * ground.vertical_km_per_day));
}

double chewing_net_energy_mj(const AnimalClassParameters& animal, const AnimalState& state,
                             const DietQuality& diet, double intake_kg_dm) noexcept {
  return state.liveweight_kg * animal.grazing_coefficient * intake_kg_dm *
         (kChewDigestibilityOffset - (diet.digestibility_percent / 100.0));
}

double energy_value_of_gain_mj_per_kg(const AnimalClassParameters& animal,
                                      const AnimalState& state) noexcept {
  // TMC Eq. 46: the adjustment for how fast the weight is being put on.
  const double empty_body_gain_g = state.liveweight_change_kg_per_day * 1000.0 * kEmptyBodyFraction;
  const double reference_scale =
      kGainRateDivisor * metabolic_weight(animal.standard_reference_weight_kg);
  const double rate_adjustment = (empty_body_gain_g / reference_scale) - 1.0;

  // TMC Eq. 45.
  const double maturity = state.liveweight_kg / animal.standard_reference_weight_kg;

  // TMC Eq. 44.
  const double sigmoid = 1.0 + std::exp(-kGainSigmoidSteepness * (maturity - kGainSigmoidMidpoint));
  return (kGainFloorMjPerKg + rate_adjustment) +
         ((animal.gain_energy_ceiling_mj_per_kg - rate_adjustment) / sigmoid);
}

double liveweight_change_net_energy_mj(const AnimalClassParameters& animal,
                                       const AnimalState& state) noexcept {
  return state.liveweight_change_kg_per_day * kEmptyBodyFraction *
         energy_value_of_gain_mj_per_kg(animal, state);
}

EnergyRequirement daily_energy_requirement(const AnimalClassParameters& animal,
                                           const AnimalState& state, const DietQuality& diet,
                                           const GrazingConditions& ground) {
  const std::string animal_error = animal.validation_error();
  if (!animal_error.empty()) {
    throw std::invalid_argument("daily_energy_requirement: " + animal_error);
  }
  const std::string diet_error = diet.validation_error();
  if (!diet_error.empty()) {
    throw std::invalid_argument("daily_energy_requirement: " + diet_error);
  }
  if (state.liveweight_kg <= 0.0) {
    throw std::invalid_argument("daily_energy_requirement: liveweight must be positive");
  }

  EnergyRequirement result;
  result.basal_net_mj = basal_net_energy_mj(animal, state);
  result.movement_net_mj = movement_net_energy_mj(state, ground);
  result.activity_net_mj = activity_net_energy_mj(state, ground);

  // TMC Eq. 81: liveweight change is converted to ME by kg, not km.
  result.liveweight_change_me_mj =
      liveweight_change_net_energy_mj(animal, state) / diet.gain_efficiency();

  const double maintenance_efficiency = diet.maintenance_efficiency();
  const double production_me = result.liveweight_change_me_mj;

  // TMC Eq. 52: the loop needs somewhere to start, and starting from the right
  // order of magnitude is what lets it finish inside five passes.
  result.chewing_net_mj = result.basal_net_mj * kInitialChewFractionOfBasal;

  double previous_maintenance = 0.0;
  for (int pass = 1; pass <= kMaximumIterations; ++pass) {
    // TMC Eq. 54.
    const double net_maintenance = result.basal_net_mj + result.chewing_net_mj +
                                   result.movement_net_mj + result.activity_net_mj;
    result.maintenance_me_mj =
        (net_maintenance / maintenance_efficiency) + (kProductionMaintenanceShare * production_me);

    // TMC Eq. 55 and 19.
    result.total_me_mj = result.maintenance_me_mj + production_me;
    result.intake_kg_dm = result.total_me_mj / diet.metabolisable_energy_mj_per_kg_dm;

    result.iterations = pass;
    if (pass > 1 &&
        std::abs(result.maintenance_me_mj - previous_maintenance) < kMaintenanceToleranceMj) {
      result.converged = true;
      break;
    }
    previous_maintenance = result.maintenance_me_mj;

    // TMC Eq. 18, with the intake this pass produced.
    result.chewing_net_mj = chewing_net_energy_mj(animal, state, diet, result.intake_kg_dm);
  }

  return result;
}

LiveweightResponse liveweight_response(const AnimalClassParameters& animal,
                                       const AnimalState& state, const DietQuality& diet,
                                       const GrazingConditions& ground, double intake_kg_dm) {
  const std::string animal_error = animal.validation_error();
  if (!animal_error.empty()) {
    throw std::invalid_argument("liveweight_response: " + animal_error);
  }
  const std::string diet_error = diet.validation_error();
  if (!diet_error.empty()) {
    throw std::invalid_argument("liveweight_response: " + diet_error);
  }
  if (state.liveweight_kg <= 0.0) {
    throw std::invalid_argument("liveweight_response: liveweight must be positive");
  }
  if (intake_kg_dm < 0.0) {
    throw std::invalid_argument("liveweight_response: intake cannot be negative");
  }

  LiveweightResponse response;
  response.metabolisable_energy_mj = intake_kg_dm * diet.metabolisable_energy_mj_per_kg_dm;

  // Chewing is not circular here: the intake is given rather than solved for,
  // which is the whole difference between this function and its inverse.
  const double net_maintenance = basal_net_energy_mj(animal, state) +
                                 chewing_net_energy_mj(animal, state, diet, intake_kg_dm) +
                                 movement_net_energy_mj(state, ground) +
                                 activity_net_energy_mj(state, ground);

  const double maintenance_efficiency = diet.maintenance_efficiency();
  response.maintenance_me_mj = net_maintenance / maintenance_efficiency;
  response.surplus_me_mj = response.metabolisable_energy_mj - response.maintenance_me_mj;
  response.losing = response.surplus_me_mj < 0.0;

  // TMC Eq. 54 charges a tenth of the production cost to maintenance, so with
  // ME available fixed the production term solves in closed form:
  //   available = net/km + 0.1 P + P, hence P = (available - net/km) / 1.1.
  //
  // The share is not charged on a deficit: it is the cost of producing, and an
  // animal losing weight is not producing. TMC does not cover that case, so
  // this is a reading rather than the manual's, and docs/validation/verify.md says so.
  const double production_me = response.losing
                                   ? response.surplus_me_mj
                                   : response.surplus_me_mj / (1.0 + kProductionMaintenanceShare);

  const double efficiency = response.losing ? diet.loss_efficiency() : diet.gain_efficiency();

  // TMC Eq. 81 rearranged: NElwt = MElwt * kg, then Eq. 43 rearranged again
  // for the weight. The energy value of gain depends on the rate, so iterate.
  const double net_liveweight_energy = production_me * efficiency;

  AnimalState working = state;
  double previous_change = 0.0;
  for (int pass = 1; pass <= kMaximumIterations; ++pass) {
    const double energy_value = energy_value_of_gain_mj_per_kg(animal, working);
    if (energy_value <= 0.0) {
      break;
    }
    response.liveweight_change_kg = net_liveweight_energy / (kEmptyBodyFraction * energy_value);
    response.iterations = pass;

    if (pass > 1 && std::abs(response.liveweight_change_kg - previous_change) < 1e-6) {
      response.converged = true;
      break;
    }
    previous_change = response.liveweight_change_kg;
    working.liveweight_change_kg_per_day = response.liveweight_change_kg;
  }

  return response;
}

}  // namespace paddock::core
