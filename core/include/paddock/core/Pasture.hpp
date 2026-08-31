// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#pragma once

#include <string>

#include <paddock/core/BudgetLedger.hpp>
#include <paddock/core/Weather.hpp>

namespace paddock::core {

/// Everything that distinguishes one pasture species from another.
///
/// Not one of these values has a default. Ryegrass and white clover differ only
/// in the numbers a TOML definition gives them, which is what makes adding
/// cocksfoot or lucerne a data change rather than a code change.
struct PastureSpeciesParameters {
  std::string species_id;

  /// Leaf area per kilogram of dry matter, m2 per kg. With dry matter in kg/ha
  /// this gives the leaf area index that drives light interception.
  double specific_leaf_area_m2_per_kg = 0.0;

  /// Beer's law extinction coefficient for the canopy.
  double extinction_coefficient = 0.0;

  /// Grams of above-ground dry matter per MJ of intercepted PAR.
  ///
  /// Published figures need care: values around 2 g/MJ for perennial ryegrass
  /// are commonly whole-plant, roots included, while this model grows only what
  /// an animal can eat. See docs/validation/verify.md.
  double radiation_use_efficiency_g_per_mj = 0.0;

  /// Cardinal temperatures for the growth response, degrees C.
  double base_temperature_c = 0.0;
  double optimum_temperature_c = 0.0;
  double maximum_temperature_c = 0.0;

  /// Fraction of green dry matter that senesces each day, applied only to the
  /// dry matter above `residual_kg_dm_per_ha`.
  double senescence_rate_per_day = 0.0;

  /// Dry matter that does not senesce, kg/ha: crown, stubble and the reserves a
  /// plant regrows from. Without it a sward grazed or dried to nothing would
  /// have no leaf area, intercept no light, and never grow again - zero would
  /// be an absorbing state, which is exactly what a real pasture is not.
  double residual_kg_dm_per_ha = 0.0;

  /// Nitrogen in green tissue, kg N per kg DM.
  double nitrogen_content_fraction = 0.0;

  /// Nitrogen fixed per tonne of dry matter grown, kg N per t DM. Zero for a
  /// grass; a legume covers its own demand from the atmosphere and gives the
  /// surplus to the soil.
  double nitrogen_fixation_kg_per_t_dm = 0.0;

  [[nodiscard]] std::string validation_error() const;
};

/// How a grazing animal's nitrogen comes back to the paddock.
///
/// Every figure here is a share or a loading rather than a rate, so a scenario
/// that changes stocking changes the nitrogen without changing any of these.
struct ExcretaParameters {
  /// Nitrogen a urine patch lands at, kg N/ha. OVERSEER's technical description
  /// for regional councils: "Urine and dung patches contain large N loads (up
  /// to 1000 kg N/ha)". This is the ceiling of that range and so the least
  /// concentrated reading of it would be lower - a lower loading spreads the
  /// same nitrogen over more ground and leaches less.
  double urine_patch_loading_kg_n_per_ha = 1000.0;

  /// What the pasture on a patch can actually take up before the rest is
  /// surplus, kg N/ha. **PLACEHOLDER**: patch uptake saturates well below the
  /// loading, which is the whole reason patches leach, but no single measured
  /// figure has been read. 300 is the order the literature discusses.
  double urine_patch_uptake_kg_n_per_ha = 300.0;

  /// Share of a day's drainage water that actually carries nitrate past the
  /// root zone, as against water that bypasses the soil matrix. One means
  /// perfect mixing. **PLACEHOLDER** - preferential flow through a stony soil
  /// is real and would lower this.
  double drainage_mixing_fraction = 1.0;

  /// Nitrogen a kilogram of DUNG carries, per kilogram of dry matter the animal
  /// ate. TMC (Animal model, Eq. 137 and its discussion): Barrow and Lambourne
  /// (1962) put average sheep dung at 0.835 g N per 100 g DM eaten, and
  /// Burgraaf (AgResearch) at 0.72. The higher figure is used, which sends more
  /// nitrogen to the slow organic path and less to urine - the conservative
  /// direction for a leaching estimate.
  double dung_nitrogen_per_kg_intake = 0.00835;

  /// Nitrogen in a kilogram of liveweight gain. Protein is roughly 18% of a
  /// sheep's gain and nitrogen is 16% of protein. **PLACEHOLDER** - no single
  /// measured figure has been read for New Zealand sheep.
  double body_nitrogen_per_kg_gain = 0.029;

  /// Nitrogen a head puts into wool each day. A 5 kg fleece at 16% nitrogen is
  /// 0.8 kg N a year. **PLACEHOLDER**, and it shares the fleece weight's
  /// unsourced 5 kg.
  double wool_nitrogen_kg_per_head_per_day = 0.0022;

  /// Nitrogen in a kilogram of bought feed.
  ///
  /// **Not a detail on this farm.** In its driest year it buys 126 tonnes of
  /// supplement against 145 tonnes grazed, so feed brought through the gate is
  /// half the nitrogen the stock eat - and all of it is excreted onto the
  /// paddock like any other. A leaching model that counted only grazed nitrogen
  /// would understate this farm by something near half. **VERIFY**: 2.5% is the
  /// order for pasture silage and baleage; no source is recorded.
  double supplement_nitrogen_fraction = 0.025;

  [[nodiscard]] std::string invalid_reason() const;
};

/// A day's dung and urine from one mob, in kg N.
struct Excreta {
  double urine_nitrogen_kg = 0.0;
  double dung_nitrogen_kg = 0.0;

  [[nodiscard]] double total_kg() const noexcept { return urine_nitrogen_kg + dung_nitrogen_kg; }
};

/// Splits what a mob ate into what it kept and what it gave back.
///
/// **The dung is computed and the urine is the remainder**, which is TMC
/// Eq. 137 read the way it is written: dung nitrogen is a fixed concentration
/// per kilogram of dry matter eaten, so whatever is excreted beyond that is
/// urine. On a 3.5% nitrogen diet that puts about 76% of excreta nitrogen in
/// urine, which is where nearly all the leaching comes from.
[[nodiscard]] Excreta excreta_from_intake(double nitrogen_eaten_kg, double intake_kg_dm,
                                          double liveweight_gain_kg, double head,
                                          const ExcretaParameters& excreta) noexcept;

/// A two-species sward and the soil nitrogen it draws on.
struct SwardParameters {
  PastureSpeciesParameters grass;
  PastureSpeciesParameters legume;

  /// Fraction of global solar radiation that is photosynthetically active.
  double par_fraction = 0.0;

  /// Fraction of the dead pool that decomposes each day. Its carbon leaves the
  /// tracked pools as soil organic matter; its nitrogen mineralises back into
  /// the soil mineral pool.
  double decomposition_rate_per_day = 0.0;

  [[nodiscard]] std::string validation_error() const;
};

/// Temperature response, dimensionless and between 0 and 1.
///
/// Zero below the base temperature, rising linearly to one at the optimum and
/// falling back to zero at the maximum. The shape matters more than its
/// smoothness: it is what stops a Canterbury July growing grass.
[[nodiscard]] double temperature_response(double mean_air_temperature_c, double base_c,
                                          double optimum_c, double maximum_c) noexcept;

/// Fraction of light intercepted by a canopy of this leaf area index (Beer).
[[nodiscard]] double light_interception(double leaf_area_index,
                                        double extinction_coefficient) noexcept;

/// What one day did to the sward, all in kg per hectare except the factors.
struct PastureGrowth {
  double intercepted_par_mj_per_m2 = 0.0;
  double temperature_factor = 0.0;
  double water_factor = 0.0;
  double nitrogen_factor = 1.0;

  double grass_growth_kg_dm = 0.0;
  double legume_growth_kg_dm = 0.0;
  double senescence_kg_dm = 0.0;
  double decomposition_kg_dm = 0.0;

  double nitrogen_fixed_kg = 0.0;
  double nitrogen_uptake_kg = 0.0;
  double nitrogen_mineralised_kg = 0.0;

  [[nodiscard]] double total_growth_kg_dm() const noexcept {
    return grass_growth_kg_dm + legume_growth_kg_dm;
  }
};

/// A grass-legume sward on one hectare, growing on light, temperature, water
/// and nitrogen.
///
/// The two species compete for light in proportion to their leaf area and
/// convert what they intercept at their own efficiency. The legume fixes its
/// own nitrogen and hands the surplus to the soil; the grass takes what the
/// soil has, and grows less when it is short. That coupling is the reason a
/// clover-rich sward carries a farm through a summer without fertiliser, and it
/// is the whole point of modelling the two species rather than "pasture".
class PastureSward {
 public:
  PastureSward(SwardParameters parameters, double grass_kg_dm_per_ha, double legume_kg_dm_per_ha,
               double soil_mineral_nitrogen_kg_per_ha);

  /// Advances one day. `water_stress_coefficient` is Ks from the soil water
  /// bucket: 1 when the profile is wet, falling to 0 at wilting point.
  ///
  /// With a ledger attached, growth is reported as a dry matter inflow,
  /// senescence as an internal transfer, decomposition as an outflow, fixation
  /// as a nitrogen inflow, and uptake and mineralisation as internal transfers.
  PastureGrowth step(const DailyWeather& weather, double water_stress_coefficient,
                     BudgetLedger* ledger = nullptr);

  [[nodiscard]] double grass_kg_dm() const noexcept { return grass_kg_dm_; }

  [[nodiscard]] double legume_kg_dm() const noexcept { return legume_kg_dm_; }

  [[nodiscard]] double dead_kg_dm() const noexcept { return dead_kg_dm_; }

  [[nodiscard]] double green_kg_dm() const noexcept { return grass_kg_dm_ + legume_kg_dm_; }

  /// Everything standing, which is what a cover meter reads.
  [[nodiscard]] double cover_kg_dm() const noexcept { return green_kg_dm() + dead_kg_dm_; }

  /// Legume share of green dry matter, 0 when there is no green.
  [[nodiscard]] double legume_fraction() const noexcept;

  /// What one defoliation took, by species.
  struct Defoliation {
    double grass_kg_dm = 0.0;
    double legume_kg_dm = 0.0;
    double nitrogen_kg = 0.0;

    [[nodiscard]] double total_kg_dm() const noexcept { return grass_kg_dm + legume_kg_dm; }
  };

  /// Removes green dry matter, and returns what was actually taken.
  ///
  /// Capped at what stands above each species' residual, for the same reason
  /// senescence is: the crown and stubble are what the plant regrows from, and
  /// a sward grazed to nothing would have no leaf area, intercept no light and
  /// never grow again. Asking for more than that is not an error - it is a farm
  /// short of feed - and the shortfall is what the caller sees in the
  /// difference between what it asked for and what it got.
  ///
  /// **Grass and legume are taken in proportion to what is on offer.** Real
  /// animals select, and Smith and Dawson (1976) report that set stocking is
  /// "highly selective" and overgrazes clover in particular. Modelling that
  /// needs a source for how strong the preference is, which this project does
  /// not have yet; see docs/validation/verify.md. Until it does, a system comparison here
  /// cannot show the species-composition half of their finding.
  ///
  /// Nitrogen leaves with the dry matter, at each species' own content.
  Defoliation remove_green_dry_matter(double requested_kg_dm);

  /// Returns a day's dung and urine to this sward, in kg N over its area.
  ///
  /// **Closing the loop the animal used to break.** Nitrogen left with the
  /// grazed dry matter and never came back, so a grazed farm ran its soil down
  /// to nothing and the nitrogen budget closed only because the offtake was
  /// booked as leaving the system. A grazing animal keeps a tenth of what it
  /// eats and returns the rest within days, which is why excreta - not
  /// fertiliser - is the primary source of nitrate leaching in New Zealand
  /// pastoral farming.
  ///
  /// **Urine and dung go to different places because they behave differently.**
  /// Urine is urea: it hydrolyses within days and nitrifies, so it arrives in
  /// the mineral pool immediately and is available to leach. Dung is organic and
  /// mineralises slowly, so it joins the dead pool and comes back through the
  /// decomposition already modelled.
  ///
  /// **And urine does not land evenly.** A ewe urinates on a patch, not on a
  /// paddock: OVERSEER's technical description puts patch loadings at up to
  /// 1000 kg N/ha, far past what a plant can take up, so most of a patch's
  /// nitrogen is surplus from the moment it lands. That surplus is what leaches,
  /// and spreading urine evenly over a hectare - which is what a model without
  /// patches does - would have the pasture absorb nearly all of it and leach
  /// almost nothing.
  void return_excreta(double urine_nitrogen_kg, double dung_nitrogen_kg,
                      const ExcretaParameters& excreta, BudgetLedger* ledger = nullptr);

  /// Leaches nitrate out of the root zone with the day's drainage, and returns
  /// what left in kg N.
  ///
  /// **A mixing model, and the simplest one that is not a guess.** Water leaving
  /// the root zone carries the nitrate dissolved in it, so the share of the
  /// pool that goes is the share of the water that goes. OVERSEER defines the
  /// root zone at 60 cm and counts nitrogen past it as lost, accounting for
  /// nothing that happens between there and a river - and neither does this.
  double leach_nitrate(double drainage_mm, double soil_water_mm, BudgetLedger* ledger = nullptr);

  [[nodiscard]] double soil_mineral_nitrogen_kg() const noexcept {
    return soil_mineral_nitrogen_kg_;
  }

  /// Nitrate sitting under urine patches: surplus to what the plants there can
  /// use, and the pool that leaching draws on.
  [[nodiscard]] double patch_nitrate_kg() const noexcept { return patch_nitrate_kg_; }

  /// Nitrogen held in plant material, green and dead.
  [[nodiscard]] double plant_nitrogen_kg() const noexcept;

  /// Every kilogram of nitrogen the model is holding: the closing stock the
  /// conservation tests compare against the ledger.
  [[nodiscard]] double total_nitrogen_kg() const noexcept {
    return soil_mineral_nitrogen_kg_ + patch_nitrate_kg_ + plant_nitrogen_kg();
  }

  [[nodiscard]] double leaf_area_index() const noexcept;

  [[nodiscard]] const SwardParameters& parameters() const noexcept { return parameters_; }

 private:
  SwardParameters parameters_;
  double grass_kg_dm_ = 0.0;
  double legume_kg_dm_ = 0.0;
  double dead_kg_dm_ = 0.0;
  /// Nitrogen in the dead pool, tracked separately because grass and legume
  /// litter arrive with different nitrogen contents and mix once they land.
  double dead_nitrogen_kg_ = 0.0;
  double soil_mineral_nitrogen_kg_ = 0.0;
  /// Nitrate under urine patches, held apart from the mineral pool because the
  /// plants on a patch cannot use it and the plants off the patch cannot reach
  /// it. It waits for drainage.
  double patch_nitrate_kg_ = 0.0;
};

}  // namespace paddock::core
