// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#pragma once

#include <cstdint>
#include <string>

/// What a grazing animal needs to eat, and why.
///
/// Every equation here is quoted from one document, and each function names the
/// equation it implements so a reader can check it against the source:
///
///   Wheeler, D M (2018). Animal metabolisable energy requirements. Technical
///   Manual for the description of the OVERSEER Nutrient Budgets engine,
///   version 6.3.0. AgResearch Ltd for OVERSEER Limited. ISSN 2253-461X.
///
/// docs/validation/verify.md carries the URL, the SHA-256 of the PDF these were read from,
/// and the primary sources the manual itself cites - chiefly Nicol and Brookes
/// (2007) and CSIRO (2007), which disagree about the species factor K in a way
/// that is recorded rather than resolved.
///
/// Nothing here is stateful and nothing draws a random number: an animal's
/// requirement on a given day is a function of its weight, age, diet and
/// ground. The herd that carries the state belongs elsewhere.
namespace paddock::core {

/// Gross energy of feed dry matter, MJ/kg. CSIRO (2007), quoted as TMC Eq. 2.
inline constexpr double kGrossEnergyMjPerKgDm = 18.4;

/// Ratio of empty body weight to liveweight, TMC Eq. 43.
inline constexpr double kEmptyBodyFraction = 0.92;

/// What the animal is eating.
struct DietQuality {
  /// dietME: metabolisable energy per kilogram of dry matter eaten.
  double metabolisable_energy_mj_per_kg_dm = 0.0;

  /// Organic matter digestibility, percent. Drives the cost of chewing
  /// (TMC Eq. 18): coarser feed costs more to process.
  double digestibility_percent = 0.0;

  /// Proportion of the diet that is milk, 0 to 1. Changes which efficiency
  /// applies (TMC Eq. 5 against Eq. 6) and so is not a cosmetic input.
  double milk_fraction = 0.0;

  /// qm, the energy density: dietME / gross energy. TMC Eq. 2.
  [[nodiscard]] double energy_density() const noexcept;

  /// km, the efficiency with which ME is used for maintenance.
  ///
  /// TMC Eq. 5 gives 0.85 for milk diets; Eq. 6 gives 0.35 qm + 0.503 for
  /// everything else, from CSIRO (2007). On a 10.5 MJ/kg pasture diet this is
  /// 0.703, and 0.28 / 0.703 = 0.40 MJ ME per kg lwt^0.75 - the figure Simpson
  /// (1978b) published for sheep. That agreement is what fixes which case is
  /// which; see docs/validation/verify.md.
  [[nodiscard]] double maintenance_efficiency() const noexcept;

  /// kg, the efficiency with which ME is used for liveweight gain.
  ///
  /// TMC Eq. 9, kgf = 0.042 dietME + 0.006, blended with milk at 0.7 by Eq. 8.
  /// The manual also states a temperate-pasture form (Eq. 11) that varies with
  /// legume content and day of year, and then says it "has not been
  /// implemented"; that form is recorded in docs/validation/verify.md and deliberately not
  /// used here, partly because the text of its seasonal term did not survive
  /// extraction unambiguously.
  [[nodiscard]] double gain_efficiency() const noexcept;

  /// The efficiency that applies when an animal is *losing* weight rather than
  /// gaining, TMC Eq. 8: km / 0.8 for a non-lactating animal.
  ///
  /// The asymmetry is real and it is the manual's, not an assumption made here.
  /// Body tissue is mobilised for maintenance more efficiently than feed energy
  /// is converted to it, so a kilogram lost buys more than a kilogram gained
  /// cost. The arithmetic is self-consistent: tissue energy used at 0.8 equals
  /// feed ME used at km.
  [[nodiscard]] double loss_efficiency() const noexcept;

  [[nodiscard]] std::string validation_error() const;
};

/// What sort of animal this is, in the sense a person looking at a farm means.
///
/// The energy model does not need this: it works from the species factor, the
/// sex factor and a standard reference weight, and a ewe and a dairy cow differ
/// in those numbers rather than in a label. A **view** does need it, because a
/// map that cannot say whether those are sheep or cattle is not telling the
/// reader the one thing they can see for themselves standing at the gate.
///
/// Declared in the species file rather than read off `class_id`. The ids happen
/// to begin "sheep_" and "cattle_" today, and a convention that works until
/// somebody adds a species is not a fact about the animal.
enum class AnimalKind : std::uint8_t {
  Sheep,
  Cattle,
  Deer,

  /// Something the views have no marker for. Drawn as such rather than as a
  /// guess at the nearest one.
  Other,
};

[[nodiscard]] std::string to_string(AnimalKind kind);
[[nodiscard]] AnimalKind animal_kind_from(const std::string& name);

/// What distinguishes one class of animal from another - a dairy cow from a
/// ewe, a stag from a weaner.
///
/// Nothing here has a default that means anything, for the same reason
/// PastureSpeciesParameters has none: an animal class is a data definition, and
/// a silent default would be a number nobody chose.
struct AnimalClassParameters {
  std::string class_id;

  /// Sheep, cattle or deer, for anything that has to show them to a person.
  AnimalKind kind = AnimalKind::Other;

  /// K, the species factor of TMC Eq. 13.
  ///
  /// The two primaries disagree. CSIRO (2007), which the manual follows for
  /// sheep, dairy and beef: 1.0 sheep, 1.4 dairy, 1.4 dairy replacements, 1.4
  /// beef, 1.25 dairy goats. Nicol and Brookes (2007): 1.0 sheep, 1.3 beef,
  /// 1.5 dairy, 1.4 deer. The manual takes 1.7 for deer from a New Zealand
  /// source. Which one a farm uses is a data decision and belongs in the TOML.
  double species_factor = 0.0;

  /// S, TMC Eq. 14: 1.15 entire males, 1.075 mixed-sex mobs, 1.0 females and
  /// castrated males.
  double sex_factor = 0.0;

  /// SRW, the standard reference weight: the mature weight of a female of the
  /// breed. Sets maturity (TMC Eq. 45) and so the energy value of gain.
  double standard_reference_weight_kg = 0.0;

  /// SpGraze, TMC Eq. 20: 0.0025 for dairy and beef.
  double grazing_coefficient = 0.0;

  /// k1 of TMC Eq. 44: 16.5 for large lean cattle breeds, 20.3 otherwise. The
  /// ceiling the energy value of gain approaches at maturity.
  double gain_energy_ceiling_mj_per_kg = 0.0;

  [[nodiscard]] std::string validation_error() const;
};

/// One animal on one day.
struct AnimalState {
  double liveweight_kg = 0.0;
  double age_days = 0.0;

  /// Positive when gaining, negative when losing. TMC Eq. 43.
  double liveweight_change_kg_per_day = 0.0;
};

/// The ground the animal is grazing, and how far it walks over it.
///
/// This is where terrain enters the livestock model. TMC Eq. 23 makes the cost
/// of movement scale with 1 + tan(slope), and Eq. 24 charges eleven times as
/// much per kilometre climbed as per kilometre walked on the flat, so the same
/// animal on the same feed costs more on a hill face than on a terrace.
struct GrazingConditions {
  /// PastureMass, t DM/ha, in TMC Eq. 22: on a heavier cover an animal walks
  /// less for the same intake.
  double pasture_mass_t_dm_per_ha = 0.0;

  double slope_degrees = 0.0;

  /// TSR/SD in TMC Eq. 21 - the stocking rate over the stock density, which is
  /// the share of the paddock one animal has to cover.
  double area_per_animal_ha = 0.0;

  /// Hkm and Vkm of TMC Eq. 24: kilometres walked horizontally and climbed
  /// vertically in a day, beyond grazing itself.
  double horizontal_km_per_day = 0.0;
  double vertical_km_per_day = 0.0;
};

/// How far an animal walks in a day, by the steepness of the ground it is on.
///
/// **The numbers are published, not chosen.** OVERSEER's *Characteristics of
/// animals* chapter (v6.3, Table 30) gives them by topography class, taken in
/// turn from Nicol and Brookes (2007, Appendix - activity costs):
///
///   | class      | Hk km/day | Vk km/day |
///   |------------|----------:|----------:|
///   | flat       |       0.5 |       0   |
///   | rolling    |       1.0 |       0.1 |
///   | easy hill  |       1.5 |       0.15|
///   | steep hill |       2.0 |       0.2 |
///
/// They are not per species: the same chapter's pasture-mass table has a row
/// per animal type and this one does not, so a ewe and a steer are charged the
/// same kilometres on the same ground.
///
/// **Where the slope boundaries come from, and why they are this project's
/// decision.** OVERSEER takes topography as a category the user picks per
/// block; this model has a measured slope for every cell and so has to map one
/// to the other. Two New Zealand sources agree on where the lines fall:
/// Manaaki Whenua's Land Use Capability slope classes (A 0-3, B 4-7, C 8-15,
/// D 16-20, E 21-25, F 26-35, G >35 degrees), and Te Ara's description of
/// pastoral land as flat to rolling 0-16, hill 16-26, steep above 26. So flat
/// is LUC A and B, rolling is C, easy hill is D and E, and steep hill is F and
/// G - and the two sources break at the same two degrees.
///
/// **This is charged on top of the movement of grazing, not instead of it.**
/// TMC Eq. 54 sums NEbasal, NEchew, NEmove and NEactivity, so a model that runs
/// Eq. 22 and Eq. 24 together is doing what OVERSEER does rather than counting
/// the same walking twice.
///
/// One thing OVERSEER is not consistent about, recorded rather than tidied
/// away: Table 30 is headed "walking distances *while grazing*", while the
/// chapter that consumes it calls NEactivity the cost of "other activities such
/// as finding water, shelter". Which of the two the distances describe does not
/// change where they go - Table 30 feeds Eq. 24 - but it does mean this term
/// should not be quoted as one or the other.
struct WalkingDistance {
  double horizontal_km_per_day = 0.0;
  double vertical_km_per_day = 0.0;
};

[[nodiscard]] WalkingDistance walking_distance_on(double slope_degrees) noexcept;

/// Agefactor, TMC Eq. 17: exp(-0.00008 a) with a in days, floored at 0.84.
///
/// **The floor is a decision, not a default**, and the two OVERSEER documents
/// disagree about it. The technical manual states "Agefactor had a minimum
/// value of 0.84 (Freer et al., 2006)". Its independent review asks why "the
/// lower bound on AgeFactor was not used as in the Freer et al. (2010)
/// expression", notes that CSIRO (2007) and Nicol and Brookes (2007) place no
/// lower bound, and says the omission "may allow ME requirements to drop too
/// low for animals older than 6 years".
///
/// This project carries the floor. It is what the manual documents and what the
/// review argues for, so the two agree on the outcome even while disagreeing
/// about what the code does. It must not be described as the behaviour of the
/// reviewed OVERSEER implementation, which the review says lacks it.
///
/// The floor binds from about 2179 days, so **every animal older than roughly
/// six years takes the same discount** - worth knowing before reading anything
/// into a mature animal's age.
///
/// The age term applies to adult stock, not only to growing ones: the review
/// works a 500 kg animal at four years through
/// `MEm = ((0.36 W^0.75)/km) exp(-0.00008 A)`. The manual also records the
/// day-based form as differing from Nicol and Brookes' and CSIRO's year-based
/// ones by about 0.14% on average.
[[nodiscard]] double age_factor(double age_days) noexcept;

/// NEbasal, TMC Eq. 13: 0.28 K S M Agefactor lwt^0.75, attributed to Nicol and
/// Brookes (2007) equation 1.
///
/// The milk factor M is 1 here: it applies only before weaning, and Nicol and
/// Brookes did not include it at all (TMC Eq. 15). Pre-weaned animals are a
/// separate case in the manual and are not modelled yet.
[[nodiscard]] double basal_net_energy_mj(const AnimalClassParameters& animal,
                                         const AnimalState& state) noexcept;

/// SlopeMoveFactor, TMC Eq. 23: 1 + tan(slope). 1.00 flat, 1.18 at 10 degrees,
/// 1.36 at 20, 1.58 at 30.
[[nodiscard]] double slope_movement_factor(double slope_degrees) noexcept;

/// NEmove, TMC Eq. 21 with the pasture-mass term of Eq. 22.
[[nodiscard]] double movement_net_energy_mj(const AnimalState& state,
                                            const GrazingConditions& ground) noexcept;

/// NEactivity, TMC Eq. 24.
[[nodiscard]] double activity_net_energy_mj(const AnimalState& state,
                                            const GrazingConditions& ground) noexcept;

/// NEchew, TMC Eq. 18. Depends on intake, which depends on the total
/// requirement, which depends on this - see daily_energy_requirement.
[[nodiscard]] double chewing_net_energy_mj(const AnimalClassParameters& animal,
                                           const AnimalState& state, const DietQuality& diet,
                                           double intake_kg_dm) noexcept;

/// EVG, TMC Eq. 44-46: the net energy in a kilogram of empty body gain, which
/// rises with maturity as the animal lays down fat rather than protein.
[[nodiscard]] double energy_value_of_gain_mj_per_kg(const AnimalClassParameters& animal,
                                                    const AnimalState& state) noexcept;

/// NElwt, TMC Eq. 43.
[[nodiscard]] double liveweight_change_net_energy_mj(const AnimalClassParameters& animal,
                                                     const AnimalState& state) noexcept;

/// What one animal needs on one day, and what it has to eat to get it.
struct EnergyRequirement {
  double basal_net_mj = 0.0;
  double chewing_net_mj = 0.0;
  double movement_net_mj = 0.0;
  double activity_net_mj = 0.0;

  /// ME rather than NE: liveweight change is converted by kg at TMC Eq. 81.
  double liveweight_change_me_mj = 0.0;

  /// MEmaintenance, TMC Eq. 54 - the net terms over km, plus a tenth of the
  /// production cost, which is Freer et al.'s way of charging the maintenance
  /// that comes with producing rather than adding it to production.
  double maintenance_me_mj = 0.0;

  /// MErequirements, TMC Eq. 55.
  double total_me_mj = 0.0;

  /// TMC Eq. 19: what the animal has to eat to cover total_me_mj.
  double intake_kg_dm = 0.0;

  /// How many passes the chewing loop took, and whether it settled. The manual
  /// stops at five whether or not it has converged (TMC section 5.2.2), so a
  /// caller that cares can see which happened.
  int iterations = 0;
  bool converged = false;
};

/// Solves the requirement, chewing cost included.
///
/// Chewing costs energy in proportion to what is eaten, and what is eaten is
/// set by the total requirement, which includes the cost of chewing. The manual
/// resolves the circle by iterating from an initial guess of
/// NEchew = 0.046 NEbasal (TMC Eq. 52) until maintenance moves by less than
/// 0.1 MJ, or five passes have gone by - a bounded loop, so the result is
/// reproducible rather than dependent on how long a solver was allowed to run.
///
/// Throws std::invalid_argument when the animal or the diet is not valid;
/// a requirement computed from a zero-energy diet would be an infinite intake.
[[nodiscard]] EnergyRequirement daily_energy_requirement(const AnimalClassParameters& animal,
                                                         const AnimalState& state,
                                                         const DietQuality& diet,
                                                         const GrazingConditions& ground);

/// What actually eating a given amount does to an animal's weight.
///
/// The inverse of daily_energy_requirement, and the direction a simulation
/// needs: that function answers "to grow this fast, what must it eat", and this
/// one answers "having eaten this, how fast does it grow". A model with only
/// the first has no way for a paddock short of feed to have any consequence.
struct LiveweightResponse {
  /// What the intake carried.
  double metabolisable_energy_mj = 0.0;

  /// What standing still cost: the net terms over km, TMC Eq. 54 without the
  /// production share.
  double maintenance_me_mj = 0.0;

  /// Positive when there was energy left over, negative when the animal had to
  /// find the difference in its own tissue.
  double surplus_me_mj = 0.0;

  /// The answer, kg per day. Negative when losing.
  double liveweight_change_kg = 0.0;

  /// True when the animal did not cover maintenance from what it ate.
  bool losing = false;

  int iterations = 0;
  bool converged = false;
};

/// Works out what a day's intake did to an animal.
///
/// The energy value of gain depends on how fast the weight is changing (TMC
/// Eq. 46), and how fast it changes is what this function is solving for, so it
/// iterates - bounded, like the chewing loop, so the answer does not depend on
/// how long a solver was allowed to run. The dependence is weak at ordinary
/// growth rates, and the loop usually settles in two passes.
///
/// **Starvation is not modelled.** An animal that cannot cover maintenance
/// loses weight here for as long as it is underfed, with no floor and no
/// mortality. A run that drives one to implausible weights is telling you the
/// feed budget is wrong, not that the animal survived it.
///
/// Throws std::invalid_argument on the same grounds daily_energy_requirement
/// does, and for a negative intake.
[[nodiscard]] LiveweightResponse liveweight_response(const AnimalClassParameters& animal,
                                                     const AnimalState& state,
                                                     const DietQuality& diet,
                                                     const GrazingConditions& ground,
                                                     double intake_kg_dm);

}  // namespace paddock::core
