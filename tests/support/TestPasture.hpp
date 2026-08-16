#pragma once

#include <paddock/core/Pasture.hpp>
#include <paddock/core/SoilWater.hpp>

namespace paddock::test_support {

/// A ryegrass and white clover sward used by several suites.
///
/// These are fixture values, **not a calibration**. They are the right order of
/// magnitude and the right shape - a grass with a low base temperature and no
/// fixation, a legume that fixes about 25 kg N per tonne of dry matter - but
/// the published ranges are wide (radiation use efficiency between 1 and 3
/// g DM/MJ depending on whether roots are counted, extinction coefficients from
/// 0.3 to 1.3), and pinning them is the job of the T3 validation gate against
/// measured DairyNZ growth curves, not of a unit test. See docs/verify.md.
inline core::SwardParameters test_sward_parameters() {
  core::SwardParameters parameters;
  parameters.par_fraction = 0.5;
  parameters.decomposition_rate_per_day = 0.02;

  core::PastureSpeciesParameters& grass = parameters.grass;
  grass.species_id = "ryegrass_perennial";
  grass.specific_leaf_area_m2_per_kg = 20.0;
  grass.extinction_coefficient = 0.5;
  grass.radiation_use_efficiency_g_per_mj = 1.5;
  grass.base_temperature_c = 4.4;
  grass.optimum_temperature_c = 20.0;
  grass.maximum_temperature_c = 35.0;
  grass.senescence_rate_per_day = 0.02;
  grass.residual_kg_dm_per_ha = 400.0;
  grass.nitrogen_content_fraction = 0.035;
  grass.nitrogen_fixation_kg_per_t_dm = 0.0;

  core::PastureSpeciesParameters& legume = parameters.legume;
  legume.species_id = "clover_white";
  legume.specific_leaf_area_m2_per_kg = 25.0;
  legume.extinction_coefficient = 0.6;
  legume.radiation_use_efficiency_g_per_mj = 1.4;
  legume.base_temperature_c = 5.0;
  legume.optimum_temperature_c = 22.0;
  legume.maximum_temperature_c = 35.0;
  legume.senescence_rate_per_day = 0.025;
  legume.residual_kg_dm_per_ha = 100.0;
  legume.nitrogen_content_fraction = 0.045;
  legume.nitrogen_fixation_kg_per_t_dm = 25.0;

  return parameters;
}

/// A moderately deep silt loam, in the shape S-map reports.
inline core::SoilWaterParameters test_soil_parameters() {
  core::SoilWaterParameters parameters;
  parameters.total_available_water_mm =
      core::SoilWaterParameters::total_available_water(0.38, 0.18, 0.6);
  parameters.depletion_fraction = 0.6;  // FAO-56 Table 22, grazed pasture
  parameters.crop_coefficient = 0.95;   // FAO-56 Table 12, rotated grazing
  parameters.runoff_fraction = 0.05;
  return parameters;
}

}  // namespace paddock::test_support
