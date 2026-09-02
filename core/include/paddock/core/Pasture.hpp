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
  /// **A fallback, used only where no leaf lifespan is stated.** A flat rate is
  /// the wrong shape for a grass: a leaf's life is measured in thermal time, so
  /// it turns over in a few days in January and takes a month in July. Setting
  /// `degree_days_per_leaf` below replaces this, and should.
  double senescence_rate_per_day = 0.0;

  /// Thermal time a tiller needs to put out one new leaf, degree-days above
  /// this species' base temperature.
  ///
  /// **This is what makes senescence a season rather than a number.** DairyNZ
  /// call ryegrass a "three leaf plant": a tiller sustains three live leaves,
  /// so when the fourth appears the first dies, and a leaf therefore lives
  /// three leaf-appearance intervals. They also state the interval directly -
  /// "a new leaf growing every 8 days in mid-spring" - and that is what this
  /// figure is derived from. Zero leaves `senescence_rate_per_day` in charge.
  double degree_days_per_leaf = 0.0;

  /// Live leaves a tiller carries before the oldest dies. Three for ryegrass,
  /// which is the whole of DairyNZ's leaf-stage grazing rule.
  double leaves_per_tiller = 3.0;

  /// **Which shape the temperature response takes**, and the exponent it takes
  /// it with. Zero gives the triangular response this model started with; a
  /// positive value gives AgPasture's C3 curve, for which it is a fitted
  /// parameter of the same set as the cardinal temperatures and should not be
  /// separated from them.
  ///
  /// AgPasture derives its own upper limit rather than stating one -
  /// `optimum + (optimum - base) / exponent` - so a sward adopting these
  /// figures should set `maximum_temperature_c` to that value and say so.
  double temperature_response_exponent = 0.0;

  /// **The spring flush, which this model has no phenology to produce.**
  ///
  /// Perennial ryegrass turns reproductive in spring: it elongates stem, runs
  /// up a seed head, and puts a far larger share of what it fixes into shoot
  /// while it does. Afterwards it carries fewer tillers and less potential
  /// through summer even with water in the soil. Paddock models none of that -
  /// growth is light times temperature times water times nitrogen - so it grows
  /// whenever conditions allow, and in Canterbury the best light and warmth are
  /// in January. Measured against Winchmore's 25 years, spring beats summer 25
  /// times out of 25 there and 8 times in 10 here (verify.md, E67).
  ///
  /// This is AgPasture's stand-in for the same missing mechanism, and its
  /// documentation is candid about being one: "Reproductive phase of perennial
  /// is not simulated by the model, the ReproductiveGrowthFactor attempts to
  /// mimic the main effect, which is a higher allocation of DM to shoot during
  /// this period."
  ///
  /// **Everything about the curve comes from latitude**, which is why adopting
  /// it is not the same as fitting a seasonal multiplier to the trial it is
  /// checked against. The season starts later and runs shorter the further from
  /// the equator, and the allocation increase is larger. At -43.641 that gives
  /// a season opening on 1 September, a 35.8-day ramp to a plateau on 7
  /// October, 59.6 days of full effect, and a 23.9-day taper ending 29
  /// December, with shoot allocation up 28.3% at the peak. Winchmore's biggest
  /// months, in order, are October, November, September, December.
  ///
  /// **Zero for the increase turns the whole thing off**, which is the default,
  /// so a sward file written before this keeps the seasons it was written with.
  /// AgPasture has it on by default; this model makes a sward say so, the same
  /// way `degree_days_per_leaf` and `temperature_response_exponent` do.
  ///
  /// Source: `InitReproductiveGrowthFactor` and `CalcReproductiveGrowthFactor`
  /// in AgPasture's `PastureSpecies`, with that class's own default values.
  /// Li FY, Snow VO & Holzworth DR (2011), NZJAR 54: 331-352.
  double repro_season_max_allocation_increase = 0.0;
  double repro_season_reference_latitude_degrees = 41.0;
  double repro_season_timing_coefficient = 0.14;
  double repro_season_duration_coefficient = 2.0;
  double repro_season_shoulders_length_factor = 1.0;
  double repro_season_onset_duration_factor = 0.60;
  double repro_season_allocation_coefficient = 0.10;

  /// **How much faster leaf dies when the plant runs out of water.** A drought
  /// does two things to a sward and this model long had only one of them: it
  /// slows the tiller down, and it kills the leaf the tiller is already
  /// carrying. Without the second, a Canterbury February loses leaf at the same
  /// rate as an October spring, holds its cover, keeps intercepting light and
  /// keeps growing - which is the whole of the seasonal error in E62.
  ///
  /// The form and all three figures are APSIM AgPasture's, whose ryegrass and
  /// white clover carry identical values. Below `threshold` the turnover rate
  /// is multiplied by `1 + max * ((threshold - Ks) / threshold) ^ exponent`,
  /// so it is unchanged in a wet spring and doubles on a soil at wilting point.
  ///
  /// Source: `MoistureEffectOnTissueTurnover` in AgPasture's `PastureSpecies`,
  /// with defaults from `AGPRyegrass.json` and `AGPWhiteClover.json`; the model
  /// is Li FY, Snow VO & Holzworth DR (2011), "Modelling the seasonal and
  /// geographical pattern of pasture production in New Zealand", NZ Journal of
  /// Agricultural Research 54: 331-352 - which is this exact problem.
  double drought_turnover_threshold = 0.6;
  double drought_turnover_effect_max = 1.0;
  double drought_turnover_exponent = 2.0;

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

  /// How much more readily nitrate under a urine patch leaches than the mineral
  /// nitrogen between the patches.
  ///
  /// **Both leach, and the model only counted one of them.** OVERSEER reports
  /// leaching from urine patches and leaching from everything else - dung,
  /// fertiliser, and soil organic matter mineralising - separately, and puts
  /// the second at "less than 15% of total N loss" on a grazed pastoral block.
  /// Counting only the patches therefore reads about that much low, which every
  /// report printing the figure had to say.
  ///
  /// Between the patches the plants get first call on the nitrogen: it is
  /// spread thin enough for them to take up, which is exactly why patches leach
  /// and inter-patch ground does not. This is the share of the mixing that
  /// reaches the drain rather than a root.
  ///
  /// **FITTED, and to OVERSEER's own statement of the answer.** There is no
  /// measurement here: the figure is set so the inter-patch term comes to about
  /// a tenth of this farm's loss, which is inside the "less than 15% of total N
  /// loss" OVERSEER reports for a grazed pastoral block. A first attempt at one
  /// eighth put it at 60% of the loss and tripled the farm's leaching, which is
  /// what a parameter with no measurement behind it does when nobody checks
  /// where it lands.
  ///
  /// The report prints the inter-patch share beside the total, so the fit is
  /// visible rather than buried: if it drifts past 15% something upstream has
  /// changed and this needs re-fitting or replacing with a measurement.
  double inter_patch_leaching_fraction = 0.012;

  [[nodiscard]] std::string invalid_reason() const;
};

/// The share of standing leaf that dies today, from the thermal time a leaf
/// lives for.
///
/// **A leaf's life is measured in degree-days, not in days.** A tiller carries
/// `leaves_per_tiller` live leaves and pushes out a new one every
/// `degree_days_per_leaf`, so the oldest dies on the same schedule: the rate is
/// one over the thermal time a leaf survives. In a Canterbury January that is
/// about 7% of the standing leaf a day; in July it is under 1%.
///
/// A flat rate - which is what this replaced - gets the annual total roughly
/// right and the season exactly wrong, and the season is what a farmer plans
/// around. It also fixes the ratio of dead to green at senescence over
/// decomposition, so a model with both at 2% a day sits at equilibrium with as
/// much dead standing as green, which is not a pasture anybody grazes.
///
/// Falls back to `senescence_rate_per_day` when no leaf lifespan is stated.
/// `water_factor` is the same water stress coefficient that scales growth, and
/// it belongs here for the reason DairyNZ state: the time a tiller takes to
/// produce a new leaf "is largely dependent on temperature **and moisture**". A
/// thirsty plant turns its leaves over more slowly, which is why a Canterbury
/// summer rotation is longer than a spring one and not shorter.
///
/// **Leaving moisture out was worth 65 kg DM/ha a day.** With temperature
/// alone, February senescence came to three times February growth and the green
/// pool collapsed - a sward dying faster than any Canterbury pasture does,
/// from reading half of a sentence.
///
/// **That was half of a sentence too.** Slowing the tiller is one of the two
/// things a drought does, and on this farm's weather it cancels the extra heat
/// of summer almost exactly - spring 7.5 degree-days times a water factor of
/// 0.618 is 4.64, summer 12.5 times 0.366 is 4.59 - so leaf lives 78 days in a
/// February drought and 78 days in an October spring. The other thing a drought
/// does is kill the leaf that is already standing, and `drought_turnover_*`
/// carries it (verify.md, E62).
[[nodiscard]] double senescence_share(const PastureSpeciesParameters& species,
                                      double mean_temperature_c, double water_factor) noexcept;

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

  /// Where this sward is, in degrees, negative south. Read only by the
  /// reproductive season, whose timing and strength are functions of it, and
  /// ignored entirely when no species asks for one.
  double latitude_degrees = 0.0;

  /// Fraction of the dead pool that decomposes each day. Its carbon leaves the
  /// tracked pools as soil organic matter; its nitrogen mineralises back into
  /// the soil mineral pool.
  double decomposition_rate_per_day = 0.0;

  [[nodiscard]] std::string validation_error() const;
};

/// Temperature response, dimensionless and between 0 and 1.
///
/// Zero below the base temperature and above the maximum, one at the optimum.
/// It is what stops a Canterbury July growing grass.
///
/// **Two shapes, and which one you get depends on whether the sward states an
/// exponent.** With `exponent` at zero the response is triangular: straight up
/// to the optimum, straight back down. With a positive exponent it is the
/// curve AgPasture uses for C3 pasture, after Thornley & Johnson:
///
///     ((T - base)^q * (max - T)) / ((optimum - base)^q * (max - optimum))
///
/// The difference is not cosmetic. A triangle understates a cool spring badly -
/// at Canterbury's 11.9 C spring mean the triangle gives 0.48 where the curve
/// gives 0.68, while at the 16.9 C summer mean the two are 0.80 and 0.95. Most
/// of a grass's response is banked early, and a straight line says otherwise
/// (verify.md, E64).
///
/// Zero is the default so that a sward file written before this existed keeps
/// the response it was written against, the same way `degree_days_per_leaf`
/// leaves a flat senescence rate in charge.
[[nodiscard]] double temperature_response(double mean_air_temperature_c, double base_c,
                                          double optimum_c, double maximum_c,
                                          double exponent = 0.0) noexcept;

/// A day's temperature factor for one species, from the day's minimum and
/// maximum rather than from their average.
///
/// **A response fitted to photosynthesis has to be asked about the temperature
/// the leaf photosynthesises at**, and that is not the 24-hour mean - half of
/// which is spent in the dark, at the coldest part of the day. AgPasture
/// evaluates its curve twice, once at the daily mean and once at a daylight
/// temperature of `0.75 * max + 0.25 * min`, and weights them one to three.
///
/// Paddock fed it the plain mean, which understates the response in proportion
/// to the diurnal range - by 30% in a Canterbury winter and 2% in its summer,
/// because a curve this far from its optimum in July is steep there and nearly
/// flat in January. Correcting it is not a new degree of freedom; it is the
/// rest of a method already adopted (verify.md, E65).
///
/// Only for the curve. A sward that states no exponent keeps the triangular
/// response on the daily mean, which is what it was written against.
[[nodiscard]] double daily_temperature_factor(const PastureSpeciesParameters& species,
                                              double min_air_temperature_c,
                                              double max_air_temperature_c) noexcept;

/// How much more of what it fixes a plant sends to shoot today, because it is
/// in its reproductive season. One outside that season, and up to
/// `1 + repro_season_max_allocation_increase` scaled by latitude within it.
///
/// The season is a trapezium: a linear ramp on, a plateau, a linear ramp off.
/// Where it falls and how long it lasts are computed from latitude alone - it
/// starts half a year after the winter solstice adjusted for how far from the
/// reference latitude the site is, and shortens towards the poles.
///
/// Returns 1.0 when the species states no increase, which is the default.
[[nodiscard]] double reproductive_growth_factor(const PastureSpeciesParameters& species,
                                                double latitude_degrees,
                                                int day_of_year) noexcept;

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

  /// Cuts the sward down to `leave_kg_dm_per_ha` of cover and returns what came
  /// off, kg DM/ha.
  ///
  /// **A mower is not an animal.** Grazing takes green in proportion to what is
  /// on offer and leaves each species' residual; a cut takes green and dead
  /// together down to a stubble. That is why cutting cleans a rank paddock up -
  /// the dead goes into the stack with the green - and why silage made off a
  /// summer surplus is poorer feed than the pasture it came from.
  ///
  /// The residuals are still respected: a cut takes nothing below the crown a
  /// plant regrows from, so a farm cannot mow itself out of existence.
  double cut_to(double leave_kg_dm_per_ha);

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
  ///
  /// **Two pools, because OVERSEER reports two.** The urine patches, where
  /// nitrogen is spread far past what a plant can take up and most of it is
  /// available to leach; and everything between them - dung, mineralising
  /// organic matter - where the plants get first call and only a fraction
  /// reaches the drain. The second is minor on a grazed block, under 15% of the
  /// loss, and leaving it out was worth exactly that much of an understatement.
  double leach_nitrate(double drainage_mm, double soil_water_mm, const ExcretaParameters& excreta,
                       BudgetLedger* ledger = nullptr);

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
