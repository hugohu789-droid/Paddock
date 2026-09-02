// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>

#include <paddock/core/Geometry.hpp>
#include <paddock/core/Pasture.hpp>

namespace paddock::core {

namespace {

/// Grams per square metre to kilograms per hectare. The hectare itself comes
/// from Geometry.hpp, where the rest of the area arithmetic lives.
constexpr double kGramsPerM2ToKgPerHa = 10.0;

constexpr double kKilogramsPerTonne = 1000.0;

double leaf_area(const PastureSpeciesParameters& species, double dry_matter_kg_per_ha) noexcept {
  return species.specific_leaf_area_m2_per_kg * dry_matter_kg_per_ha / kSquareMetresPerHectare;
}

}  // namespace

std::string PastureSpeciesParameters::validation_error() const {
  if (species_id.empty()) {
    return "species_id must not be empty";
  }
  const std::string where = " (" + species_id + ")";
  if (specific_leaf_area_m2_per_kg <= 0.0) {
    return "specific_leaf_area_m2_per_kg must be positive" + where;
  }
  if (extinction_coefficient <= 0.0) {
    return "extinction_coefficient must be positive" + where;
  }
  if (radiation_use_efficiency_g_per_mj <= 0.0) {
    return "radiation_use_efficiency_g_per_mj must be positive" + where;
  }
  if (base_temperature_c >= optimum_temperature_c ||
      optimum_temperature_c >= maximum_temperature_c) {
    return "cardinal temperatures must satisfy base < optimum < maximum" + where;
  }
  if (senescence_rate_per_day < 0.0 || senescence_rate_per_day >= 1.0) {
    return "senescence_rate_per_day must be between 0 and 1" + where;
  }
  if (residual_kg_dm_per_ha < 0.0) {
    return "residual_kg_dm_per_ha must not be negative" + where;
  }
  if (nitrogen_content_fraction <= 0.0 || nitrogen_content_fraction >= 1.0) {
    return "nitrogen_content_fraction must be between 0 and 1" + where;
  }
  if (nitrogen_fixation_kg_per_t_dm < 0.0) {
    return "nitrogen_fixation_kg_per_t_dm must not be negative" + where;
  }
  return {};
}

std::string SwardParameters::validation_error() const {
  std::string species_error = grass.validation_error();
  if (!species_error.empty()) {
    return species_error;
  }
  species_error = legume.validation_error();
  if (!species_error.empty()) {
    return species_error;
  }
  if (par_fraction <= 0.0 || par_fraction > 1.0) {
    return "par_fraction must be between 0 and 1";
  }
  if (decomposition_rate_per_day < 0.0 || decomposition_rate_per_day >= 1.0) {
    return "decomposition_rate_per_day must be between 0 and 1";
  }
  return {};
}

double temperature_response(double mean_air_temperature_c, double base_c, double optimum_c,
                            double maximum_c) noexcept {
  if (mean_air_temperature_c <= base_c || mean_air_temperature_c >= maximum_c) {
    return 0.0;
  }
  if (mean_air_temperature_c <= optimum_c) {
    return (mean_air_temperature_c - base_c) / (optimum_c - base_c);
  }
  return (maximum_c - mean_air_temperature_c) / (maximum_c - optimum_c);
}

double light_interception(double leaf_area_index, double extinction_coefficient) noexcept {
  if (leaf_area_index <= 0.0 || extinction_coefficient <= 0.0) {
    return 0.0;
  }
  return 1.0 - std::exp(-extinction_coefficient * leaf_area_index);
}

PastureSward::PastureSward(SwardParameters parameters, double grass_kg_dm_per_ha,
                           double legume_kg_dm_per_ha, double soil_mineral_nitrogen_kg_per_ha)
    : parameters_(std::move(parameters)),
      grass_kg_dm_(std::max(0.0, grass_kg_dm_per_ha)),
      legume_kg_dm_(std::max(0.0, legume_kg_dm_per_ha)),
      soil_mineral_nitrogen_kg_(std::max(0.0, soil_mineral_nitrogen_kg_per_ha)) {
  const std::string error = parameters_.validation_error();
  if (!error.empty()) {
    throw std::invalid_argument("PastureSward: " + error);
  }
}

double PastureSward::legume_fraction() const noexcept {
  const double green = green_kg_dm();
  return green > 0.0 ? legume_kg_dm_ / green : 0.0;
}

std::string ExcretaParameters::invalid_reason() const {
  if (urine_patch_loading_kg_n_per_ha <= 0.0) {
    return "a urine patch has to land at some nitrogen";
  }
  if (urine_patch_uptake_kg_n_per_ha < 0.0) {
    return "patch uptake cannot be negative";
  }
  if (urine_patch_uptake_kg_n_per_ha > urine_patch_loading_kg_n_per_ha) {
    return "a patch cannot take up more nitrogen than lands on it - if it could, "
           "urine patches would not leach and they are what does";
  }
  if (drainage_mixing_fraction <= 0.0 || drainage_mixing_fraction > 1.0) {
    return "drainage_mixing_fraction is a share and must lie in (0, 1]";
  }
  return {};
}

double senescence_share(const PastureSpeciesParameters& species, double mean_temperature_c,
                        double water_factor) noexcept {
  if (species.degree_days_per_leaf <= 0.0 || species.leaves_per_tiller <= 0.0) {
    return std::clamp(species.senescence_rate_per_day, 0.0, 1.0);
  }

  // Thermal time this species banked today. Below its base temperature a grass
  // makes no new leaf, and the one it is carrying does not die on schedule
  // either - which is why a winter sward holds its cover for weeks.
  const double degree_days = std::max(0.0, mean_temperature_c - species.base_temperature_c);

  // **Thermal time alone**, which is how AgPasture drives tissue turnover. The
  // water factor used to multiply this line as well, on the argument that a
  // thirsty tiller takes longer to push out its next leaf. That is true of leaf
  // *appearance*; applied to leaf *death* it cancelled the drought term below
  // almost exactly, and left a February drought turning leaf over at an October
  // rate (verify.md, E62).
  if (degree_days <= 0.0) {
    return 0.0;
  }

  const double days_per_leaf = species.degree_days_per_leaf / degree_days;
  const double leaf_lifespan_days = species.leaves_per_tiller * days_per_leaf;

  // **And the half of drought the line above does not carry.** Slowing the
  // tiller keeps leaf alive; drying the plant out kills the leaf it is already
  // holding. AgPasture's form, and its ryegrass and clover figures: nothing
  // below the threshold, rising as the square of how far past it the soil has
  // gone, to double the turnover on a profile at wilting point.
  double drought = 1.0;
  const double stress = std::clamp(water_factor, 0.0, 1.0);
  if (species.drought_turnover_threshold > 0.0 && stress < species.drought_turnover_threshold) {
    const double past = (species.drought_turnover_threshold - stress) /
                        species.drought_turnover_threshold;
    drought = 1.0 + (species.drought_turnover_effect_max *
                     std::pow(past, species.drought_turnover_exponent));
  }

  // One over the lifespan: the share of standing leaf that reaches the end of
  // it today. Clamped, because a hot enough day would otherwise kill more leaf
  // than is standing.
  return std::clamp(drought / leaf_lifespan_days, 0.0, 1.0);
}

Excreta excreta_from_intake(double nitrogen_eaten_kg, double intake_kg_dm,
                            double liveweight_gain_kg, double head,
                            const ExcretaParameters& excreta) noexcept {
  Excreta out;
  if (nitrogen_eaten_kg <= 0.0) {
    return out;
  }

  // What the animal keeps: what it laid down as body, and what it grew as wool.
  // Everything else comes back out.
  const double retained =
      (std::max(0.0, liveweight_gain_kg) * head * excreta.body_nitrogen_per_kg_gain) +
      (excreta.wool_nitrogen_kg_per_head_per_day * head);

  const double excreted = std::max(0.0, nitrogen_eaten_kg - retained);

  // TMC Eq. 137: dung carries a fixed concentration per kilogram eaten, and the
  // urine is what is left. Capped at the excreted total, because an animal on a
  // diet poorer in nitrogen than its own dung cannot excrete more than it ate.
  out.dung_nitrogen_kg =
      std::min(excreted, std::max(0.0, intake_kg_dm) * excreta.dung_nitrogen_per_kg_intake);
  out.urine_nitrogen_kg = excreted - out.dung_nitrogen_kg;
  return out;
}

void PastureSward::return_excreta(double urine_nitrogen_kg, double dung_nitrogen_kg,
                                  const ExcretaParameters& excreta, BudgetLedger* ledger) {
  const double urine = std::max(0.0, urine_nitrogen_kg);
  const double dung = std::max(0.0, dung_nitrogen_kg);
  if (urine <= 0.0 && dung <= 0.0) {
    return;
  }

  // **The patch, in one line.** Urine lands at a loading, the plants under it
  // take up what they can, and the share of the nitrogen that is surplus is the
  // share of the loading they could not reach. It does not depend on how much
  // urine there was - a bigger crop of urine makes more patches, not richer
  // ones - which is why this is a fraction and not a subtraction.
  const double surplus_share =
      1.0 - (excreta.urine_patch_uptake_kg_n_per_ha / excreta.urine_patch_loading_kg_n_per_ha);

  const double to_patch = urine * surplus_share;
  const double to_soil = urine - to_patch;

  patch_nitrate_kg_ += to_patch;
  soil_mineral_nitrogen_kg_ += to_soil;

  // Dung is organic and joins the litter, mineralising through the decomposition
  // this sward already models rather than arriving as nitrate.
  dead_nitrogen_kg_ += dung;

  if (ledger != nullptr) {
    // **An inflow, because it is one.** The nitrogen in grazed dry matter is
    // booked out of this system when the animal eats it, and the animal is not
    // in this system - so what it gives back arrives from outside. A farm whose
    // stock ate and never returned anything ran its soil to nothing and the
    // budget still closed, which is what an outflow with no matching inflow
    // does.
    ledger->record_inflow(Budget::Nitrogen, "excreta_urine", urine);
    ledger->record_inflow(Budget::Nitrogen, "excreta_dung", dung);
  }
}

double PastureSward::leach_nitrate(double drainage_mm, double soil_water_mm,
                                   const ExcretaParameters& excreta, BudgetLedger* ledger) {
  if (drainage_mm <= 0.0) {
    return 0.0;
  }

  // The share of the water in the root zone that left today is the share of the
  // nitrate dissolved in it that went with it.
  const double water = std::max(0.0, soil_water_mm) + drainage_mm;
  const double share = water > 0.0 ? std::clamp(drainage_mm / water, 0.0, 1.0) : 0.0;
  if (share <= 0.0) {
    return 0.0;
  }

  // **The patches**, where the nitrogen is past what a plant can use and is
  // simply waiting for water.
  const double from_patches = patch_nitrate_kg_ * share;
  patch_nitrate_kg_ -= from_patches;

  // **And between them.** Mineral nitrogen off the patches is spread thin
  // enough for the plants to have first call on it, so only a fraction of what
  // the water could carry actually goes - which is why OVERSEER puts this at
  // under 15% of a grazed block's loss and why a model counting only patches
  // reads about that much low.
  const double inter_patch_share =
      share * std::clamp(excreta.inter_patch_leaching_fraction, 0.0, 1.0);
  const double from_soil = std::max(0.0, soil_mineral_nitrogen_kg_) * inter_patch_share;
  soil_mineral_nitrogen_kg_ -= from_soil;

  if (ledger != nullptr) {
    // Past the root zone is out of this model, the way OVERSEER treats 60 cm:
    // what happens between there and a river is somebody else's question.
    // Booked apart, because a report that could not separate them could not say
    // which of a farm's two losses management can move.
    if (from_patches > 0.0) {
      ledger->record_outflow(Budget::Nitrogen, "nitrate_leaching", from_patches);
    }
    if (from_soil > 0.0) {
      ledger->record_outflow(Budget::Nitrogen, "nitrate_leaching_inter_patch", from_soil);
    }
  }
  return from_patches + from_soil;
}

double PastureSward::cut_to(double leave_kg_dm_per_ha) {
  const double standing = cover_kg_dm();
  const double floor = std::max(0.0, leave_kg_dm_per_ha);
  if (standing <= floor) {
    return 0.0;
  }

  // **The residuals hold**, so a farm cannot mow itself out of existence: the
  // crown and stubble a plant regrows from are not cuttable however low the
  // target.
  const double residual =
      parameters_.grass.residual_kg_dm_per_ha + parameters_.legume.residual_kg_dm_per_ha;
  const double target = std::max(floor, residual);
  if (standing <= target) {
    return 0.0;
  }

  // **A mower takes what is there in the proportions it is there**, green and
  // dead together - which is why cutting cleans a rank paddock up, and why the
  // silage carries the dead with it.
  const double share = (standing - target) / standing;

  const double grass_cut =
      std::max(0.0, grass_kg_dm_ - parameters_.grass.residual_kg_dm_per_ha) * share;
  const double legume_cut =
      std::max(0.0, legume_kg_dm_ - parameters_.legume.residual_kg_dm_per_ha) * share;
  const double dead_cut = dead_kg_dm_ * share;

  grass_kg_dm_ -= grass_cut;
  legume_kg_dm_ -= legume_cut;
  dead_kg_dm_ -= dead_cut;

  // The nitrogen leaves with it. Dead nitrogen in proportion to the dead taken;
  // the green at each species' own content.
  const double dead_nitrogen = dead_nitrogen_kg_ * share;
  dead_nitrogen_kg_ -= dead_nitrogen;

  return grass_cut + legume_cut + dead_cut;
}

PastureSward::Defoliation PastureSward::remove_green_dry_matter(double requested_kg_dm) {
  Defoliation taken;
  if (requested_kg_dm <= 0.0) {
    return taken;
  }

  // Only what stands above each residual is on offer, for the same reason
  // senescence works that way: below it is the crown the plant regrows from.
  const double grass_offered =
      std::max(0.0, grass_kg_dm_ - parameters_.grass.residual_kg_dm_per_ha);
  const double legume_offered =
      std::max(0.0, legume_kg_dm_ - parameters_.legume.residual_kg_dm_per_ha);
  const double offered = grass_offered + legume_offered;
  if (offered <= 0.0) {
    return taken;
  }

  const double eaten = std::min(requested_kg_dm, offered);

  // In proportion to what is on offer, not to what is standing: a species
  // already at its residual contributes nothing and must not be drawn down.
  taken.grass_kg_dm = eaten * (grass_offered / offered);
  taken.legume_kg_dm = eaten - taken.grass_kg_dm;

  grass_kg_dm_ -= taken.grass_kg_dm;
  legume_kg_dm_ -= taken.legume_kg_dm;

  taken.nitrogen_kg = (taken.grass_kg_dm * parameters_.grass.nitrogen_content_fraction) +
                      (taken.legume_kg_dm * parameters_.legume.nitrogen_content_fraction);
  return taken;
}

double PastureSward::plant_nitrogen_kg() const noexcept {
  return (grass_kg_dm_ * parameters_.grass.nitrogen_content_fraction) +
         (legume_kg_dm_ * parameters_.legume.nitrogen_content_fraction) + dead_nitrogen_kg_;
}

double PastureSward::leaf_area_index() const noexcept {
  return leaf_area(parameters_.grass, grass_kg_dm_) + leaf_area(parameters_.legume, legume_kg_dm_);
}

PastureGrowth PastureSward::step(const DailyWeather& weather, double water_stress_coefficient,
                                 BudgetLedger* ledger) {
  PastureGrowth growth;
  growth.water_factor = std::clamp(water_stress_coefficient, 0.0, 1.0);

  const double grass_leaf_area = leaf_area(parameters_.grass, grass_kg_dm_);
  const double legume_leaf_area = leaf_area(parameters_.legume, legume_kg_dm_);
  const double total_leaf_area = grass_leaf_area + legume_leaf_area;

  // Species compete for light in proportion to the leaf area each carries, and
  // the canopy's extinction coefficient is their leaf-area-weighted mean: a
  // sward that is mostly clover intercepts light the way clover does.
  const double extinction = total_leaf_area > 0.0
                                ? ((parameters_.grass.extinction_coefficient * grass_leaf_area) +
                                   (parameters_.legume.extinction_coefficient * legume_leaf_area)) /
                                      total_leaf_area
                                : 0.0;
  growth.intercepted_par_mj_per_m2 = std::max(0.0, weather.solar_radiation_mj_per_m2) *
                                     parameters_.par_fraction *
                                     light_interception(total_leaf_area, extinction);

  const double mean_temperature = weather.mean_air_temperature_c();
  const double grass_temperature = temperature_response(
      mean_temperature, parameters_.grass.base_temperature_c,
      parameters_.grass.optimum_temperature_c, parameters_.grass.maximum_temperature_c);
  const double legume_temperature = temperature_response(
      mean_temperature, parameters_.legume.base_temperature_c,
      parameters_.legume.optimum_temperature_c, parameters_.legume.maximum_temperature_c);
  growth.temperature_factor =
      total_leaf_area > 0.0
          ? ((grass_temperature * grass_leaf_area) + (legume_temperature * legume_leaf_area)) /
                total_leaf_area
          : grass_temperature;

  const double grass_share = total_leaf_area > 0.0 ? grass_leaf_area / total_leaf_area : 0.0;
  const double legume_share = total_leaf_area > 0.0 ? legume_leaf_area / total_leaf_area : 0.0;

  const double grass_potential = parameters_.grass.radiation_use_efficiency_g_per_mj *
                                 growth.intercepted_par_mj_per_m2 * grass_share *
                                 grass_temperature * growth.water_factor * kGramsPerM2ToKgPerHa;
  const double legume_potential = parameters_.legume.radiation_use_efficiency_g_per_mj *
                                  growth.intercepted_par_mj_per_m2 * legume_share *
                                  legume_temperature * growth.water_factor * kGramsPerM2ToKgPerHa;

  // Fixation covers much of a legume's nitrogen but not all of it: the
  // published 20-25 kg N fixed per tonne of dry matter sits below the 40-45 kg
  // a tonne of clover actually contains, so clover draws on the soil as well as
  // on the air. What makes it worth having is that the fixed share is new
  // nitrogen entering the system, which litter then returns to the soil.
  const double fixation_per_kg =
      parameters_.legume.nitrogen_fixation_kg_per_t_dm / kKilogramsPerTonne;
  const double soil_demand_per_kg =
      std::max(0.0, parameters_.legume.nitrogen_content_fraction - fixation_per_kg);
  const double surplus_per_kg =
      std::max(0.0, fixation_per_kg - parameters_.legume.nitrogen_content_fraction);

  const double legume_soil_demand = legume_potential * soil_demand_per_kg;
  const double legume_factor = legume_soil_demand > 0.0
                                   ? std::min(1.0, soil_mineral_nitrogen_kg_ / legume_soil_demand)
                                   : 1.0;
  growth.legume_growth_kg_dm = legume_potential * legume_factor;

  growth.nitrogen_fixed_kg = growth.legume_growth_kg_dm * fixation_per_kg;
  const double legume_uptake = growth.legume_growth_kg_dm * soil_demand_per_kg;
  const double fixation_surplus = growth.legume_growth_kg_dm * surplus_per_kg;

  const double available_nitrogen = soil_mineral_nitrogen_kg_ - legume_uptake + fixation_surplus;

  // The grass grows on what the soil can actually supply.
  const double grass_demand = grass_potential * parameters_.grass.nitrogen_content_fraction;
  const double grass_uptake = std::min(grass_demand, available_nitrogen);
  growth.nitrogen_factor = grass_demand > 0.0 ? grass_uptake / grass_demand : 1.0;
  growth.grass_growth_kg_dm = grass_potential * growth.nitrogen_factor;
  growth.nitrogen_uptake_kg = legume_uptake + grass_uptake;

  grass_kg_dm_ += growth.grass_growth_kg_dm;
  legume_kg_dm_ += growth.legume_growth_kg_dm;
  soil_mineral_nitrogen_kg_ += fixation_surplus - growth.nitrogen_uptake_kg;

  // Senescence moves dry matter and its nitrogen from green to dead, but only
  // what stands above the residual: the crown and stubble a plant regrows from
  // do not senesce away, so a sward can be grazed or dried back hard and still
  // recover.
  //
  // **How much dies is a function of the day's temperature**, through the
  // thermal time a leaf lives for - fast in January, slow in July. A flat rate
  // gets the annual total roughly right and the season exactly wrong, and it
  // was the reason this farm's cover peaked in February where a Canterbury
  // farm's bottoms out.
  const double grass_senescence =
      std::max(0.0, grass_kg_dm_ - parameters_.grass.residual_kg_dm_per_ha) *
      senescence_share(parameters_.grass, mean_temperature, growth.water_factor);
  const double legume_senescence =
      std::max(0.0, legume_kg_dm_ - parameters_.legume.residual_kg_dm_per_ha) *
      senescence_share(parameters_.legume, mean_temperature, growth.water_factor);
  growth.senescence_kg_dm = grass_senescence + legume_senescence;
  grass_kg_dm_ -= grass_senescence;
  legume_kg_dm_ -= legume_senescence;
  dead_kg_dm_ += growth.senescence_kg_dm;
  dead_nitrogen_kg_ += (grass_senescence * parameters_.grass.nitrogen_content_fraction) +
                       (legume_senescence * parameters_.legume.nitrogen_content_fraction);

  // Decomposition takes carbon out of the tracked pools into soil organic
  // matter, and mineralises the nitrogen back where the plants can reach it.
  growth.decomposition_kg_dm = dead_kg_dm_ * parameters_.decomposition_rate_per_day;
  growth.nitrogen_mineralised_kg = dead_nitrogen_kg_ * parameters_.decomposition_rate_per_day;
  dead_kg_dm_ -= growth.decomposition_kg_dm;
  dead_nitrogen_kg_ -= growth.nitrogen_mineralised_kg;
  soil_mineral_nitrogen_kg_ += growth.nitrogen_mineralised_kg;

  if (ledger != nullptr) {
    ledger->record_inflow(Budget::DryMatter, "pasture_growth", growth.total_growth_kg_dm());
    ledger->record_internal_transfer(Budget::DryMatter, "senescence", growth.senescence_kg_dm);
    ledger->record_outflow(Budget::DryMatter, "decomposition", growth.decomposition_kg_dm);

    ledger->record_inflow(Budget::Nitrogen, "legume_fixation", growth.nitrogen_fixed_kg);
    ledger->record_internal_transfer(Budget::Nitrogen, "plant_uptake", growth.nitrogen_uptake_kg);
    ledger->record_internal_transfer(Budget::Nitrogen, "fixation_surplus_to_soil",
                                     fixation_surplus);
    ledger->record_internal_transfer(Budget::Nitrogen, "residue_mineralisation",
                                     growth.nitrogen_mineralised_kg);
  }

  return growth;
}

}  // namespace paddock::core
