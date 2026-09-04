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

/// **The day a ewe's milk peaks, and it is not a parameter of this model.**
///
/// TMC Eq. 35 writes the lactation curve as `1.01 * exp(0.41 * ln(d) - 0.0287 *
/// d)`, and the day it peaks falls straight out of those two fitted constants:
/// `d(0.41 * ln d - 0.0287 * d)/dd = 0` at `d = 0.41 / 0.0287`, or about
/// **14.29 days**.
///
/// **It is derived, not chosen, and it is not configurable.** 1.01, 0.41 and
/// 0.0287 are one published regression; moving this number means re-fitting
/// OVERSEER's equation rather than setting a value, which is not something this
/// project does to somebody else's curve. It is named here so that the
/// distinction can be tested and cited instead of rediscovered.
///
/// **Not to be confused with `appetite_lactation_peak_days`**, which is
/// GrazPlan's C_I8 and dates the appetite response, not the milk. The two are
/// independent by construction: nothing in `AnimalClassParameters` reaches this
/// constant, and `daily_milk_yield_kg` reads no appetite parameter. See
/// verify.md, E109 and E110.
inline constexpr double kMilkYieldPeakDays = 0.41 / 0.0287;

/// kp, the efficiency with which ME is used for pregnancy. TMC Eq. 4.
///
/// A flat 0.13, and strikingly low beside maintenance's 0.70: growing a lamb is
/// the most expensive thing a ewe does with a megajoule, which is why late
/// pregnancy dominates a ewe's feed demand before a drop of milk is made.
inline constexpr double kPregnancyEfficiency = 0.13;

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

  /// kl, the efficiency with which ME is used for lactation. TMC Eq. 3:
  /// 0.35 qm + 0.42, which is 0.62 on a 10.5 MJ/kg pasture diet.
  [[nodiscard]] double lactation_efficiency() const noexcept;

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

  /// What one head of this class counts as in stock units, or zero when no
  /// published conversion has been recorded for it.
  ///
  /// **A reporting convention rather than a process**, and it lives here anyway
  /// because it travels with the class and because a farmer deciding how many
  /// to winter decides in these units. A New Zealand stock unit is Parker's
  /// (1998) base ewe - 55 kg, one lamb, 550 kg DM a year - and every farm in
  /// the country is benchmarked in them.
  ///
  /// Zero is "not rated", and a farm holding an unrated class declines to
  /// report a stocking rate rather than reporting one that is short.
  double stock_units = 0.0;

  /// Sheep, cattle or deer, for anything that has to show them to a person.
  AnimalKind kind = AnimalKind::Other;

  /// **How big this animal ought to be for its age**, in Brody's three
  /// coefficients. GrazPlan scales appetite by relative size and relative
  /// condition rather than by raw liveweight, and both are measured against a
  /// normal weight rather than against the standard reference weight:
  ///
  ///     Nmax = SRW - (SRW - Wbirth) * exp(-C_N1 * age / SRW^C_N2)      Eq. 1
  ///     N    = C_N3 * Nmax + (1 - C_N3) * W   when W < Nmax            Eq. 1a
  ///     Z    = N / SRW, capped at one         BC = W / N
  ///
  /// Equation 1a is what lets a stunted animal catch up: normal weight goes on
  /// rising through a hard season even while the animal is not gaining, so the
  /// animal comes out of it light for its frame rather than small. Sheep take
  /// `0.0157, 0.27, 0.4`.
  double normal_weight_rate = 0.0;
  double normal_weight_exponent = 0.27;
  double normal_weight_blend = 0.4;

  /// **What a fat animal does**, GrazPlan Eq. 3: `BC * (C_I20 - BC) / (C_I20 -
  /// 1)` once relative condition passes one, and one below that.
  ///
  /// It does not stop the animal eating. It reduces appetite until intake meets
  /// maintenance and no further, so a well-conditioned animal settles at its
  /// weight instead of growing without limit - which is what an animal on good
  /// feed actually does, and what this model had nothing to produce. Only for
  /// non-lactating animals: a ewe in milk has somewhere to put the energy.
  ///
  /// `C_I20 = 1.5` for sheep and cattle alike.
  double condition_intake_limit = 1.5;

  /// **What a milking female does to appetite**, GrazPlan Eq. 8, which is the
  /// other half of why appetite is not a constant:
  ///
  ///     LF = 1 + C_I19,Y * M^C_I9 * exp(C_I9 * (1 - M)),  M = days / C_I8
  ///
  /// A ewe at peak lactation eats about half again what a dry ewe eats, and
  /// without it she is capped below her own requirement on every day she is
  /// milking - which is what stopped intake capacity being wired into the farm
  /// at all (verify.md, E71).
  ///
  /// `C_I19` is indexed by the number of young: sheep of the wool breeds, which
  /// is where GrazPlan files the Romney, take `0.524, 0.524, 0.707, 0.891` for
  /// none, one, two and three. Sheep take `C_I8 = 28` days and `C_I9 = 1.4`.
  ///
  /// **These are appetite parameters and nothing else, which is why they carry
  /// the prefix** (verify.md, E110). `appetite_lactation_peak_days` is C_I8, the
  /// day the *appetite* increment peaks - `M^C_I9 * exp(C_I9 * (1 - M))` is at
  /// its maximum where `M = 1`. It has no bearing on when milk peaks: that
  /// belongs to TMC Eq. 35, whose own fitted constants put it at
  /// `kMilkYieldPeakDays`, and no parameter in this struct can move it. The two
  /// timings are independent, and cattle prove it - `C_I8 = 624` there, which
  /// would be nonsense as a statement about a cow's milk. Named
  /// `lactation_peak_days` until E110, where the name had already misled a
  /// review twice without ever producing a wrong number.
  ///
  /// **Two of Eq. 8's terms are not here.** LA carries condition at
  /// parturition and LB the weight lost since it, and both need a history this
  /// model does not keep. Both are at most one, so leaving them out makes a ewe
  /// hungrier than GrazPlan would and never less.
  double appetite_lactation_peak_days = 28.0;
  double appetite_lactation_curve_exponent = 1.4;
  double appetite_lactation_peak_no_young = 0.0;
  double appetite_lactation_peak_one_young = 0.0;
  double appetite_lactation_peak_two_young = 0.0;
  double appetite_lactation_peak_three_young = 0.0;

  /// **Appetite**, in GrazPlan's two coefficients: the amount of dry matter this
  /// class eats in a day with unrestricted access to good feed.
  ///
  /// Every other number in this header answers "what does this animal need".
  /// This one answers "how much would it eat if nothing stopped it", and the
  /// two are not the same - an appetite comfortably exceeds a maintenance
  /// requirement, which is why a mob on a good paddock can afford to lose a
  /// quarter of its grazing efficiency and still walk away full.
  ///
  /// GrazPlan Eq. 2, without the four factors this model has no state for:
  ///
  ///     Imax = C_I1 * SRW * Z * (C_I2 - Z)
  ///
  /// Z is relative size, liveweight over standard reference weight, capped at
  /// one. Sheep take `0.04, 1.7` and cattle `0.025, 1.7`.
  ///
  /// **The reading of C_I2 is checked twice**, because the paper's parameter
  /// table does not survive text extraction with its columns aligned. The text
  /// says the quadratic peaks at a relative size of 0.85, and the derivative of
  /// `1.7 Z - Z^2` is zero at exactly 0.85; and its Fig. 2 draws a 50 kg-SRW
  /// sheep peaking near 1.44 kg DM a day, which the equation reproduces.
  ///
  /// **Two of GrazPlan's factors are implemented and two are not.** Condition
  /// (CF, Eq. 3) and lactation (LF, Eq. 8) are both applied in
  /// `potential_intake_kg_dm`; rumen development in unweaned young (YF) and
  /// heat (TF) are not, and each of those would only reduce intake. Zero for
  /// `_scalar` turns appetite off entirely. Corrected in E110 - E96 recorded
  /// that this note still named CF and LF as missing after E75 implemented
  /// them, so the header understated what the model does.
  double appetite_scalar_per_day = 0.0;
  double appetite_size_coefficient = 1.7;

  /// **What a short paddock does to intake**, in GrazPlan's three coefficients.
  ///
  /// Everything else in this header answers "what does this animal need". This
  /// answers a different question - "how much of that can it actually get into
  /// itself" - and until it existed the model had no answer at all: a mob ate
  /// its full requirement unless the paddock physically ran out above the
  /// residual. That is a cliff where grazing is a curve. Two irrigation arms
  /// differing by 5,290 kg DM/ha of growth ate five kilograms apart (E52).
  ///
  /// A grazing animal's intake falls off well before the sward is bare, because
  /// a shorter sward means a smaller bite. It grazes longer to compensate - up
  /// to 1.6 times as long - and past a point cannot compensate at all.
  ///
  /// GrazPlan, equations 14, 16 and 17, reduced to one herbage pool:
  ///
  ///     rate = 1 - exp(-C_R4 * B)              relative rate of eating
  ///     time = 1 + C_R5 * exp(-(C_R6 * B)^2)   relative time spent grazing
  ///     relative intake = rate * time
  ///
  /// where B is standing herbage in kg DM/ha, cut close to ground level, which
  /// is how the parameters are defined and therefore green and dead together.
  ///
  /// Sheep take `1.12e-3, 0.6, 1.12e-3` and cattle `0.78e-3, 0.6, 0.74e-3`.
  /// Zero for `_rate` turns the whole thing off, which is the default, so an
  /// animal file written before this keeps the appetite it was written with.
  ///
  /// Source: Freer M, Moore AD & Donnelly JR, "The GRAZPLAN animal biology
  /// model for sheep and cattle and the GrazFeed decision support tool", CSIRO
  /// Plant Industry Technical Paper, Table 2 - the same CSIRO lineage the
  /// OVERSEER manual above cites for its own species factors. The form is from
  /// Allden and Whittaker (1970). docs/validation/verify.md carries the URL and
  /// the hash.
  double intake_availability_rate_per_kg_dm = 0.0;
  double intake_grazing_time_increase = 0.6;
  double intake_grazing_time_rate_per_kg_dm = 0.0;

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

  /// GL, the gestation length in days. TMC Table 28 gives sheep 150, from Freer
  /// et al. (2006). Zero means this class does not breed, and both the
  /// pregnancy and the lactation terms stay at zero for it.
  double gestation_length_days = 0.0;

  /// Milk fat and protein, percent, which set the energy in a kilogram of milk
  /// through TMC Eq. 46.
  ///
  /// **VERIFY, and it matters more than most.** Protein is the 5.8% commonly
  /// reported for sheep; fat is the weak one, because published fat runs from
  /// 4.6% in Iraqi Kurdi to 12.6% in Dorset ewes and no New Zealand Romney
  /// figure has been read yet. 7.0 sits mid-range. The energy in milk moves
  /// about 0.38 MJ/kg for each point of fat, so a Dorset flock would be some
  /// 40% dearer to milk than this says. docs/validation/verify.md, E23.
  double milk_fat_percent = 0.0;
  double milk_protein_percent = 0.0;

  /// Breedeffect, TMC Eq. 38: 0.2 East Friesian, 0.1 Romney/EF cross, 0.01
  /// otherwise. It scales the milk a ewe gives for twins and triplets.
  double breed_effect = 0.01;

  /// How many weeks the young of this species can suckle for. TMC Eq. 16, from
  /// SCA (1994): 26 weeks for sheep, 18 for cattle. It sets how fast milk gives
  /// way to grass in a young animal's diet, and zero means this class is never
  /// on a mother.
  double suckling_weeks = 0.0;

  [[nodiscard]] std::string validation_error() const;
};

/// The share of its potential intake an animal can harvest from a sward
/// carrying `herbage_kg_dm_per_ha`, between 0 and roughly 1.
///
/// One at a herbage mass that does not restrict grazing, falling towards zero
/// as the paddock goes bare. Returns 1.0 for an animal that states no
/// availability coefficient, which is the default and the behaviour this model
/// had before: eat the requirement, whatever is standing.
///
/// **Nothing in this model grazes through this yet, and the reason is worth
/// stating here rather than only in verify.md** (E71). This term multiplies an
/// appetite, never a requirement: it is below one at any cover a real farm
/// carries - 0.90 at 2,000 kg DM/ha - so against a requirement it would leave
/// every mob permanently short. Against `potential_intake_kg_dm` it has the
/// headroom it was designed for, and bites only below roughly 800 kg DM/ha.
///
/// That works for a dry animal and fails for a milking one, because the
/// lactation factor LF that lifts GrazPlan's potential intake is not
/// implemented - it needs body condition, which this model does not carry, and
/// a peak-intake parameter this project has not been able to read reliably.
/// Wired into the farm without it, a ewe with a lamb at foot was capped under
/// her own requirement, went short every day of lactation, and was sold: the
/// flock fell from 332 head and the farm ate a fifth of what it should.
[[nodiscard]] double relative_intake(const AnimalClassParameters& animal,
                                     double herbage_kg_dm_per_ha) noexcept;

/// One animal on one day.
struct AnimalState {
  double liveweight_kg = 0.0;
  double age_days = 0.0;

  /// Positive when gaining, negative when losing. TMC Eq. 43.
  double liveweight_change_kg_per_day = 0.0;

  /// Days since conception, or zero when the animal is not pregnant. Counted
  /// rather than dated so the energy model stays a function of state and never
  /// has to know what day of the year it is.
  int days_pregnant = 0;

  /// Days since lambing, or zero when the animal is not lactating.
  int days_lactating = 0;

  /// Young carried or reared, per animal. **Not an integer**: a mob's
  /// representative ewe rears the flock's mean, and a 132.3% lambing is 1.323
  /// lambs a ewe, not one ewe with one lamb and another with two.
  double young = 0.0;

  /// Whether this animal is still on its mother, and so getting part of its
  /// diet as milk rather than as grass.
  ///
  /// **The flag that stops the farm being fed twice.** A lamb's milk is already
  /// paid for on the ewe's side, as her lactation; charging the lamb's whole
  /// requirement to the paddock as well would count the same feed on both
  /// sides of the udder.
  bool on_the_mother = false;

  /// Energy a suckling animal gets from its mother each day, MJ.
  ///
  /// **Supply, not appetite.** It is the ewe's daily milk yield (TMC Eq. 35)
  /// shared among her lambs, at the energy Eq. 46 puts in a kilogram of it - so
  /// a lamb gets what its mother actually produced, and a ewe on a bare paddock
  /// milks less and her lambs go to the grass earlier. Setting this from the
  /// lamb's own appetite instead would let a hungry lamb conjure milk.
  ///
  /// **Carried at its net energy content**, which is what the ewe was charged
  /// for putting into it, so no energy is created crossing the udder. The
  /// manual would use it at km = 0.85 for a milk diet (TMC Eq. 5) where this
  /// model uses the pasture diet's efficiency; see docs/validation/verify.md,
  /// E25.
  double milk_me_mj_per_day = 0.0;
};

/// How big this animal ought to be for its age, kg. GrazPlan Eqs. 1 and 1a.
/// Falls back to the liveweight when no growth coefficients are stated.
[[nodiscard]] double normal_weight_kg(const AnimalClassParameters& animal,
                                      const AnimalState& state) noexcept;

/// Normal weight over standard reference weight, capped at one. GrazPlan's Z.
[[nodiscard]] double relative_size(const AnimalClassParameters& animal,
                                   const AnimalState& state) noexcept;

/// Liveweight over normal weight: above one this animal is carrying condition,
/// below one it is light for its frame. GrazPlan's BC.
[[nodiscard]] double relative_condition(const AnimalClassParameters& animal,
                                        const AnimalState& state) noexcept;

/// What this animal would eat in a day given unrestricted access to good feed,
/// kg DM per head. GrazPlan Eq. 2; see `appetite_scalar_per_day`.
///
/// Returns zero for a class that states no scalar, which callers read as "this
/// model has no appetite for this animal" and which is the default.
[[nodiscard]] double potential_intake_kg_dm(const AnimalClassParameters& animal,
                                            const AnimalState& state) noexcept;

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

/// M, the milk factor of TMC Eq. 15 with the Mage of Eq. 16, from SCA (1994).
///
/// It raises a suckling animal's basal requirement - milk is a richer diet and
/// an animal on it runs hotter - and it falls linearly to 1 as the animal grows
/// out of suckling. `basal_net_energy_mj` carried it as a hardcoded 1 until
/// there was a pre-weaned animal in the model to apply it to.
[[nodiscard]] double milk_factor(const AnimalClassParameters& animal,
                                 const AnimalState& state) noexcept;

/// propmilk, TMC Eq. 74: the share of a suckling animal's diet that is milk.
///
/// Falls from 1 at birth to 0 at the end of suckling - 182 days for a sheep,
/// which is Eq. 16's 26 weeks. **The rest is grass**, and this is what says how
/// much of a lamb's appetite the paddock has to answer for.
[[nodiscard]] double milk_share_of_diet(const AnimalClassParameters& animal,
                                        const AnimalState& state) noexcept;

/// NEmilk, TMC Eq. 46: the net energy in a kilogram of ewe milk, from its fat
/// and protein. Nicol and Brookes (2007), by way of the manual.
[[nodiscard]] double milk_net_energy_mj_per_kg(const AnimalClassParameters& animal) noexcept;

/// Birth weight of one lamb, TMC (Characteristics of animals) Eq. 11-14: a
/// share of the standard reference weight that falls as the litter grows -
/// 0.100 of SRW for a single, 0.085 twins, 0.070 triplets, 0.055 quads. The
/// same shares appear as NBW in TMC Eq. 31, which is what makes the condition
/// factor dimensionless.
///
/// `young` is a mean rather than a count, so this interpolates between the
/// published shares instead of choosing one: a flock at 1.32 lambs sits between
/// singles and twins, and so does its lambs' birth weight.
[[nodiscard]] double birth_weight_kg(const AnimalClassParameters& animal, double young) noexcept;

/// NEpregnancy, TMC Eq. 26 with the gestation proportion of Eq. 28 and the
/// condition factor of Eq. 29-31. Freer et al. (2006), with BCfoet replaced by
/// a condition factor.
///
/// **Nearly all of it lands in the last six weeks.** The Eq. 26 shape returns
/// 0.2% of its final value at conception and 100% at term, which is the whole
/// reason a ewe's feed demand climbs before lambing rather than after mating.
[[nodiscard]] double pregnancy_net_energy_mj(const AnimalClassParameters& animal,
                                             const AnimalState& state) noexcept;

/// Daily milk yield in kg, TMC Eq. 35 for sheep, with the multiple-young factor
/// of Eq. 36 and the breed factor of Eq. 38. Litherland (pers. comm.), by way
/// of the manual.
///
/// **Pasture mass is in the equation**, which is unusual and useful: a ewe on a
/// bare paddock gives less milk. The quadratic term turns the response over, so
/// grass beyond about 3,400 kg DM/ha buys no more milk.
[[nodiscard]] double daily_milk_yield_kg(const AnimalClassParameters& animal,
                                         const AnimalState& state,
                                         const GrazingConditions& ground) noexcept;

/// NElactation, TMC Eq. 33: the day's milk times the energy in a kilogram of it.
[[nodiscard]] double lactation_net_energy_mj(const AnimalClassParameters& animal,
                                             const AnimalState& state,
                                             const GrazingConditions& ground) noexcept;

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

  /// MEpregnancy, TMC Eq. 49: NEpregnancy over kp.
  ///
  /// **Stands beside production rather than inside it.** TMC Eq. 1 reads
  /// `MEmaintenance + productionME + MEpregnancy`, so pregnancy is not charged
  /// the tenth of production that Eq. 54 adds to maintenance. Lactation is.
  double pregnancy_me_mj = 0.0;

  /// MElactation, TMC Eq. 50: NElactation over kl. Part of productionME.
  double lactation_me_mj = 0.0;

  /// MEmaintenance, TMC Eq. 54 - the net terms over km, plus a tenth of the
  /// production cost, which is Freer et al.'s way of charging the maintenance
  /// that comes with producing rather than adding it to production.
  double maintenance_me_mj = 0.0;

  /// MErequirements, TMC Eq. 55.
  double total_me_mj = 0.0;

  /// ME the animal gets from its mother rather than from the paddock, TMC
  /// Eq. 74. Zero for anything not suckling, which is everything except a lamb
  /// between birth and weaning.
  double milk_me_mj = 0.0;

  /// TMC Eq. 19: what the animal has to eat **off the paddock** to cover
  /// total_me_mj, which is the whole requirement less what came as milk.
  ///
  /// For everything that is not on a mother these are the same number, so this
  /// changed no existing result when milk arrived.
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

  /// **What she paid out before any of it could become weight**: the milk she
  /// made and the lamb she is carrying, MJ ME. These are the same terms
  /// `daily_energy_requirement` charges, and they belong here for the same
  /// reason - a ewe who ate in order to lactate has spent that energy, not
  /// stored it. Zero for a dry animal.
  double lactation_me_mj = 0.0;
  double pregnancy_me_mj = 0.0;

  /// Positive when there was energy left over, negative when the animal had to
  /// find the difference in its own tissue. Net of maintenance *and* of the
  /// production above, so a ewe milking harder than she is fed loses condition,
  /// which is what a ewe milking harder than she is fed does.
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
