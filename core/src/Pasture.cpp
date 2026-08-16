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
  if (!(base_temperature_c < optimum_temperature_c &&
        optimum_temperature_c < maximum_temperature_c)) {
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
  const std::string grass_error = grass.validation_error();
  if (!grass_error.empty()) {
    return grass_error;
  }
  const std::string legume_error = legume.validation_error();
  if (!legume_error.empty()) {
    return legume_error;
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

  double available_nitrogen = soil_mineral_nitrogen_kg_ - legume_uptake + fixation_surplus;

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
  const double grass_senescence =
      std::max(0.0, grass_kg_dm_ - parameters_.grass.residual_kg_dm_per_ha) *
      parameters_.grass.senescence_rate_per_day;
  const double legume_senescence =
      std::max(0.0, legume_kg_dm_ - parameters_.legume.residual_kg_dm_per_ha) *
      parameters_.legume.senescence_rate_per_day;
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
