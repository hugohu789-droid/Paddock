// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
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

double potential_intake_kg_dm(const AnimalClassParameters& animal,
                              double liveweight_kg) noexcept {
  if (animal.appetite_scalar_per_day <= 0.0 || animal.standard_reference_weight_kg <= 0.0) {
    return 0.0;
  }

  // Relative size, capped at one: a mature animal is not more than full grown,
  // and past the cap the quadratic would start taking appetite away again.
  const double relative_size =
      std::min(1.0, std::max(0.0, liveweight_kg) / animal.standard_reference_weight_kg);

  const double appetite = animal.appetite_scalar_per_day * animal.standard_reference_weight_kg *
                          relative_size * (animal.appetite_size_coefficient - relative_size);
  return std::max(0.0, appetite);
}

double relative_intake(const AnimalClassParameters& animal,
                       double herbage_kg_dm_per_ha) noexcept {
  if (animal.intake_availability_rate_per_kg_dm <= 0.0) {
    return 1.0;
  }
  const double herbage = std::max(0.0, herbage_kg_dm_per_ha);

  // **The bite gets smaller as the sward gets shorter** (GrazPlan Eq. 16). One
  // pool rather than GrazPlan's six: this model has no digestibility classes to
  // graze selectively down, so every animal meets the whole sward at once.
  const double rate = 1.0 - std::exp(-animal.intake_availability_rate_per_kg_dm * herbage);

  // **And the animal grazes longer to make up for it** (Eq. 17), up to
  // 1 + C_R5 times as long on a bare paddock, with the compensation gone by the
  // time there is a reasonable cover.
  const double squared = animal.intake_grazing_time_rate_per_kg_dm * herbage;
  const double time = 1.0 + (animal.intake_grazing_time_increase * std::exp(-squared * squared));

  return std::max(0.0, rate * time);
}

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

double DietQuality::lactation_efficiency() const noexcept {
  // TMC Eq. 3.
  return (0.35 * energy_density()) + 0.42;
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

double milk_factor(const AnimalClassParameters& animal, const AnimalState& state) noexcept {
  if (!state.on_the_mother || animal.suckling_weeks <= 0.0) {
    return 1.0;
  }
  // TMC Eq. 16: Mage is 0.26 spread over the weeks the young can suckle, which
  // is 0.010 for sheep at 26 weeks.
  const double per_week = 0.26 / animal.suckling_weeks;

  // TMC Eq. 15, floored at 1 as the manual requires.
  return std::max(1.0, 1.0 + 0.26 - (per_week * (state.age_days / 7.0)));
}

double milk_share_of_diet(const AnimalClassParameters& animal, const AnimalState& state) noexcept {
  if (!state.on_the_mother) {
    return 0.0;
  }
  // TMC Eq. 74, clamped as the manual requires. For a sheep this is
  // 1 - age/182: all milk at birth, all grass at 26 weeks.
  return std::clamp((milk_factor(animal, state) - 1.0) / 0.26, 0.0, 1.0);
}

double basal_net_energy_mj(const AnimalClassParameters& animal, const AnimalState& state) noexcept {
  // TMC Eq. 13, with the milk factor M of Eq. 15 - 1 for anything weaned, which
  // is what Nicol and Brookes assume throughout.
  return kBasalMjPerKgMetabolic * animal.species_factor * animal.sex_factor *
         milk_factor(animal, state) * age_factor(state.age_days) *
         metabolic_weight(state.liveweight_kg);
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

WalkingDistance walking_distance_on(double slope_degrees) noexcept {
  // OVERSEER Characteristics of animals v6.3, Table 30, from Nicol and Brookes
  // (2007). The boundaries are LUC slope classes: flat is A and B, rolling is
  // C, easy hill is D and E, steep hill is F and G. See the header.
  //
  // A negative slope is not a thing the terrain model produces, but reading one
  // as flat rather than as steep keeps a bad raster from inventing energy.
  if (slope_degrees < 8.0) {
    return {0.5, 0.0};
  }
  if (slope_degrees < 16.0) {
    return {1.0, 0.1};
  }
  if (slope_degrees < 26.0) {
    return {1.5, 0.15};
  }
  return {2.0, 0.2};
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

double milk_net_energy_mj_per_kg(const AnimalClassParameters& animal) noexcept {
  // TMC Eq. 46, the sheep and beef form. The dairy form (Eq. 45) differs only
  // in its constant, 0.948 against 0.976.
  return (0.376 * animal.milk_fat_percent) + (0.209 * animal.milk_protein_percent) + 0.976;
}

double birth_weight_kg(const AnimalClassParameters& animal, double young) noexcept {
  // TMC Eq. 11-14 (Characteristics of animals): the share of SRW a lamb is born
  // at, falling as the litter grows. fRS, the size adjustment of Eq. 15, is 1
  // here because RS is measured against a reference flock this model does not
  // carry - see docs/validation/verify.md.
  static constexpr std::array<double, 4> kShare = {0.100, 0.085, 0.070, 0.055};

  const double litter = std::clamp(young, 1.0, 4.0);
  const auto lower = static_cast<std::size_t>(std::floor(litter)) - 1;
  const std::size_t upper = std::min(lower + 1, std::size_t{3});
  const double between = litter - std::floor(litter);
  const double share = kShare[lower] + ((kShare[upper] - kShare[lower]) * between);

  return share * animal.standard_reference_weight_kg;
}

double pregnancy_net_energy_mj(const AnimalClassParameters& animal,
                               const AnimalState& state) noexcept {
  if (animal.gestation_length_days <= 0.0 || state.days_pregnant <= 0 || state.young <= 0.0) {
    return 0.0;
  }

  // TMC Eq. 28: how far through gestation the animal is.
  const double gestation_proportion =
      std::clamp(static_cast<double>(state.days_pregnant) / animal.gestation_length_days, 0.0, 1.0);
  const double remaining = 1.0 - gestation_proportion;

  // TMC Eq. 26's NEpreg2. It runs from about 0.002 at conception to 1 at term,
  // which is the whole shape of a ewe's pregnancy demand.
  const double growth =
      std::exp((0.965 * remaining) + (4.37 * (1.0 - std::exp(0.965 * remaining))));

  const double birth_weight = birth_weight_kg(animal, state.young);

  // TMC Eq. 30: what the conceptus weighs now, and Eq. 31's NBW, which is the
  // same share of SRW that birth_weight_kg applied - so the ratio below is
  // dimensionless, as the manual intends.
  const double conceptus_now = birth_weight * std::exp(2.20 * (1.0 - std::exp(1.77 * remaining)));

  // TMC Eq. 29: the condition factor, RC being current weight over normal
  // weight. A ewe in better condition than her class expects carries a dearer
  // pregnancy, which is the point of the term.
  const double normal_weight = animal.standard_reference_weight_kg;
  const double condition_ratio = normal_weight > 0.0 ? state.liveweight_kg / normal_weight : 1.0;
  const double condition_factor =
      normal_weight > 0.0 && birth_weight > 0.0
          ? 1.0 + ((condition_ratio - 1.0) * (conceptus_now / birth_weight))
          : 1.0;

  // TMC Eq. 26.
  return state.young * birth_weight * 1.43 * 4.33 * (4.37 * 0.965) / animal.gestation_length_days *
         growth * condition_factor;
}

double daily_milk_yield_kg(const AnimalClassParameters& animal, const AnimalState& state,
                           const GrazingConditions& ground) noexcept {
  if (animal.gestation_length_days <= 0.0 || state.days_lactating <= 0 || state.young <= 0.0) {
    return 0.0;
  }

  const auto day = static_cast<double>(state.days_lactating);
  const double litter = std::clamp(state.young, 1.0, 4.0);
  const double singles = std::clamp(2.0 - litter, 0.0, 1.0);
  const double twins = 1.0 - singles;

  // TMC Eq. 36. The published constants are per litter size; a mob's mean
  // litter sits between two of them, so they are blended the same way birth
  // weight is. Triplets and quads share a constant in the manual because the
  // plot had flattened, so a flock this side of 2 lambs never reaches them.
  const double multiple_young = (1.002884363 * singles) + (1.287356551 * twins);

  // TMC Eq. 38, the mob form of the breed effect.
  const double breed = singles + ((1.0 + animal.breed_effect) * twins);

  // TMC Eq. 35. The lactation curve peaks around day 14 and decays; the pasture
  // terms add milk on a good paddock and take it away on a bare one.
  const double curve =
      1.01 * std::exp((0.41 * std::log(day)) - (0.0287 * day)) * multiple_young * breed * 1000.0;

  // The manual writes this term as `PastureMass * 1000 - 1300`, so its pasture
  // mass is in tonnes and its threshold in kilograms - which is exactly the
  // unit this model already carries.
  const double above_bare = (ground.pasture_mass_t_dm_per_ha * 1000.0) - 1300.0;
  const double pasture = (0.4144 * above_bare) - (1e-4 * above_bare * above_bare);

  return std::max(0.0, (curve + pasture) / 1000.0);
}

double lactation_net_energy_mj(const AnimalClassParameters& animal, const AnimalState& state,
                               const GrazingConditions& ground) noexcept {
  // TMC Eq. 33.
  return daily_milk_yield_kg(animal, state, ground) * milk_net_energy_mj_per_kg(animal);
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

  // TMC Eq. 50 and 49: net energy over its own efficiency. Pregnancy's kp of
  // 0.13 is what makes a late-pregnant ewe so expensive to feed.
  result.lactation_me_mj =
      lactation_net_energy_mj(animal, state, ground) / diet.lactation_efficiency();
  result.pregnancy_me_mj = pregnancy_net_energy_mj(animal, state) / kPregnancyEfficiency;

  const double maintenance_efficiency = diet.maintenance_efficiency();
  const double milk_share = milk_share_of_diet(animal, state);

  // **Lactation is production; pregnancy is not.** TMC Eq. 1 reads
  // `MEmaintenance + productionME + MEpregnancy`, and Eq. 54 charges a tenth of
  // production to maintenance - so which side of the line a term falls on
  // changes the answer by that tenth.
  const double production_me = result.liveweight_change_me_mj + result.lactation_me_mj;

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

    // TMC Eq. 55 and 19, with Eq. 1's pregnancy term.
    result.total_me_mj = result.maintenance_me_mj + production_me + result.pregnancy_me_mj;

    // **What the mother supplies, the paddock does not.** A suckling animal
    // takes what milk there is - the ewe's yield, not its own appetite - and
    // grazes for the rest. TMC Eq. 74's milk share caps it, so a lamb never
    // draws more of its diet from milk than the manual says it can.
    //
    // The milk was already charged to the ewe as lactation, which is what makes
    // this a transfer rather than a second helping. For anything weaned both
    // terms are zero and these two lines are the old one.
    result.milk_me_mj = std::min(state.milk_me_mj_per_day, result.total_me_mj * milk_share);
    result.intake_kg_dm = std::max(0.0, result.total_me_mj - result.milk_me_mj) /
                          diet.metabolisable_energy_mj_per_kg_dm;

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

  // **What it drank counts as much as what it ate.** Subtracting milk from a
  // lamb's grazing demand and then judging its weight on the grass alone is how
  // a lamb crop went from 3 kg to 3 grams over a spring: it asked for little,
  // got what it asked for, and was assessed as starving. The milk is real
  // energy and the ewe has already paid for it.
  response.metabolisable_energy_mj = (intake_kg_dm * diet.metabolisable_energy_mj_per_kg_dm) +
                                     std::max(0.0, state.milk_me_mj_per_day);

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
