// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

// The energy model against New Zealand industry tables.
//
// Reading a CSV and checking the CSV would prove nothing, so nothing here does
// that. Every assertion compares what `core/AnimalEnergy.cpp` computes against
// what DairyNZ or Beef + Lamb NZ published, and the two tables disagree with
// the model by very different amounts. Both are worth a test, for opposite
// reasons: the first says the model is right, and the second says exactly how
// far wrong it is so that the distance cannot drift unnoticed.

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <string>

#include <paddock/core/AnimalEnergy.hpp>

#include "support/CalibrationTable.hpp"

namespace paddock::core {
namespace {

constexpr double kPi = 3.14159265358979323846;

std::string table_path(const std::string& name) {
  return std::string(PADDOCK_DATA_DIR) + "/calibration/livestock/" + name;
}

/// Maintenance ME the way both published tables mean it: the basal requirement
/// over the efficiency with which the diet supplies it. No activity, no
/// production - just standing still.
double maintenance_me(double liveweight_kg, double species_factor, double diet_me) {
  AnimalClassParameters animal;
  animal.class_id = "reference";
  animal.species_factor = species_factor;
  animal.sex_factor = 1.0;
  animal.standard_reference_weight_kg = liveweight_kg * 2.0;  // irrelevant to maintenance
  animal.grazing_coefficient = 0.0;
  animal.gain_energy_ceiling_mj_per_kg = 20.3;

  AnimalState state;
  state.liveweight_kg = liveweight_kg;
  // The age factor is deliberately not applied: see the DairyNZ test below,
  // which is what established that it should not be here.
  state.age_days = 0.0;

  DietQuality diet;
  diet.metabolisable_energy_mj_per_kg_dm = diet_me;
  diet.digestibility_percent = 75.0;

  return basal_net_energy_mj(animal, state) / diet.maintenance_efficiency();
}

// VALIDATION. Adjabui et al. (2025) put the three New Zealand frameworks
// through one set of assumptions, which is the comparison this project could
// not make for itself - and it locates this model's sheep shortfall precisely
// enough to say what is wrong rather than only how much.
//
// Adjabui, J.A., Morel, P.H.C., Morris, S.T., Kenyon, P.R., Tozer, P.R. (2025),
// "A comparison of three nutritional models for estimating total metabolisable
// energy requirements for a ewe, beef breeding cow, lamb, and a calf/yearling
// in New Zealand's pasture-only system", Livestock Science 299:105766.
//
// **It settles what the published Nicol and Brookes figure contains.** Their
// Eq. 2 writes that model's maintenance as the basal term over km PLUS
// MEgraze, MEmove and MEactivity. This project's other comparison against the
// same source uses the basal term alone, so the 14.8% it records at 60 kg was
// two different quantities being subtracted.
//
// Their Table 1: a 60 kg ewe, four years old, standard reference weight 65 kg,
// pasture mass 3.5 t DM/ha, terrain 1.5. Their daily maintenance for a ewe
// neither pregnant nor lactating: 9.7 MJ ME/d under Nicol and Brookes (2017),
// 9.0 under CSIRO (2007), 9.9 under the AIM (MPI 2022).
//
// Two things cannot be reproduced exactly and are stated rather than hidden.
// Their baseline uses AIM's month-by-month pasture quality, so 11.0 MJ ME/kg DM
// is taken from the middle of their own sensitivity range. And "terrain 1.5" is
// a multiplier in their formulation rather than a slope: TMC Eq. 23 makes the
// same factor 1 + tan(slope), so 1.5 is 26.6 degrees.
TEST(LivestockCalibrationTest, SheepMaintenanceAgainstAdjabuiWithGrazingIncluded) {
  // The paper's ewe, from its Table 1. The grazing coefficient is this
  // project's own sheep value, which is the cattle figure carried across and is
  // marked placeholder in data/species/sheep-ewe.toml.
  AnimalClassParameters ewe;
  ewe.class_id = "sheep_ewe";
  ewe.kind = AnimalKind::Sheep;
  ewe.species_factor = 1.0;
  ewe.sex_factor = 1.0;
  ewe.standard_reference_weight_kg = 65.0;
  ewe.grazing_coefficient = 0.0025;
  ewe.gain_energy_ceiling_mj_per_kg = 20.3;

  AnimalState state;
  state.liveweight_kg = 60.0;
  state.age_days = 4.0 * 365.0;
  state.liveweight_change_kg_per_day = 0.0;

  GrazingConditions ground;
  ground.pasture_mass_t_dm_per_ha = 3.5;
  ground.slope_degrees = std::atan(0.5) * 180.0 / kPi;
  // Hill country stocking, since their Table 1 does not state one. The sweep
  // below shows the choice barely matters, which is itself the point.
  ground.area_per_animal_ha = 1.0 / 10.0;

  DietQuality diet;
  diet.metabolisable_energy_mj_per_kg_dm = 11.0;
  diet.digestibility_percent = 75.0;

  // THE BASAL TERM AGREES. Their equation computed here rather than taken on
  // trust, so what follows is a comparison and not an assumption.
  const double theirs_basal =
      0.28 * std::pow(60.0, 0.75) * std::exp(-0.03 * 4.0) / diet.maintenance_efficiency();
  const double ours_basal = basal_net_energy_mj(ewe, state) / diet.maintenance_efficiency();
  EXPECT_NEAR(ours_basal, theirs_basal, theirs_basal * 0.01)
      << "the two basal terms are the same equation and should give the same answer: theirs "
      << theirs_basal << ", ours " << ours_basal;

  // AND THE REST DOES NOT. Both published totals sit well above that basal term,
  // and the difference is what each framework charges for grazing, walking and
  // activity. This model charges a small fraction of it.
  //
  // **CSIRO is the comparator that means something here, and Nicol and Brookes
  // is reported beside it rather than instead of it.** The paper's own
  // discussion records that CSIRO accounts for chewing and ruminating inside
  // km, while Nicol and Brookes add those costs separately on top of the same
  // km - and that AgResearch (2016) asked the authors to correct the model for
  // the double counting that follows. This model's km is used the CSIRO way, so
  // measuring it against the figure that carries a known double count would
  // book somebody else's arithmetic as this project's error.
  constexpr double kPublishedCsiro = 9.0;
  constexpr double kPublishedNicolAndBrookes = 9.7;

  const EnergyRequirement need = daily_energy_requirement(ewe, state, diet, ground);
  const double our_grazing_terms = need.maintenance_me_mj - ours_basal;
  const double their_grazing_terms = kPublishedCsiro - theirs_basal;

  EXPECT_GT(their_grazing_terms, 1.4) << "the published figure carries a substantial grazing cost";
  EXPECT_LT(our_grazing_terms, their_grazing_terms / 5.0)
      << "this model's grazing, movement and activity terms come to " << our_grazing_terms
      << " MJ ME/d against the " << their_grazing_terms << " implied by CSIRO's total";

  // The two are far enough apart that which one is quoted changes the number a
  // reader takes away, which is why the report gives both.
  EXPECT_GT(kPublishedNicolAndBrookes - kPublishedCsiro, 0.5)
      << "if these ever converge, the reason for preferring CSIRO here should be revisited";

  // **The activity term is zero in every run this project makes.** TMC Eq. 24
  // charges for kilometres walked beyond grazing, and nothing ever supplies a
  // distance: Farm::conditions_on fills the pasture mass, the slope and the
  // area per animal, and leaves horizontal_km_per_day and vertical_km_per_day
  // at zero. The equation is implemented, tested in isolation, and fed by
  // nothing - the same shape of gap terrain had before it was wired up. See
  // docs/validation/verify.md, engineering caveat E10.
  EXPECT_DOUBLE_EQ(need.activity_net_mj, 0.0)
      << "if this ever becomes non-zero somebody has started supplying a distance, and the "
         "caveat about it should go";

  const double against_csiro = (kPublishedCsiro - need.maintenance_me_mj) / kPublishedCsiro;
  const double against_nicol =
      (kPublishedNicolAndBrookes - need.maintenance_me_mj) / kPublishedNicolAndBrookes;

  // Both are pinned, because the report quotes both and a change that moved one
  // without the other would go unnoticed.
  EXPECT_NEAR(against_csiro, 0.15, 0.02) << "against CSIRO: " << need.maintenance_me_mj;
  EXPECT_NEAR(against_nicol, 0.21, 0.02) << "against Nicol and Brookes: " << need.maintenance_me_mj;

  GTEST_LOG_(INFO) << "basal: theirs " << theirs_basal << ", ours " << ours_basal
                   << "; maintenance: CSIRO " << kPublishedCsiro << ", Nicol and Brookes "
                   << kPublishedNicolAndBrookes << ", ours " << need.maintenance_me_mj << " ("
                   << (against_csiro * 100.0) << "% and " << (against_nicol * 100.0) << "% low)";
}

// VALIDATION. DairyNZ publish maintenance ME by liveweight for a lactating cow,
// stated as calculated at 11.0 MJ ME/kg DM. The implemented equations reproduce
// it across the whole published range.
//
// This is the strongest evidence this project has for cattle, and it is
// stronger than the Simpson sheep check because it spans a range rather than a
// point: a wrong exponent or a wrong efficiency would show as a trend across
// the weights, not as a constant offset.
TEST(LivestockCalibrationTest, TheModelReproducesDairyNzMaintenance) {
  const test::CalibrationTable table(table_path("dairynz_maintenance_me.csv"));
  ASSERT_EQ(table.size(), 7U);

  double worst = 0.0;
  for (std::size_t row = 0; row < table.size(); ++row) {
    const double liveweight = table.number(row, "liveweight_kg");
    const double published = table.number(row, "maintenance_me_mj_per_day");
    const double modelled = maintenance_me(liveweight, 1.4, 11.0);

    const double relative = std::abs(modelled - published) / published;
    worst = std::max(worst, relative);

    EXPECT_LT(relative, 0.03) << liveweight << " kg: DairyNZ " << published << ", model "
                              << modelled;
  }
  GTEST_LOG_(INFO) << "worst deviation from DairyNZ across 300-600 kg: " << (100.0 * worst) << "%";
}

// The age factor is what decides whether the agreement above holds, so it gets
// its own assertion rather than a comment.
//
// TMC Eq. 17 discounts maintenance by age, reaching its floor of 0.84 at about
// six years. Applying it to a mature cow puts the model well below DairyNZ.
// Which of the two is right is open item 11; what this test does is make sure
// the choice is visible rather than buried in whichever value a caller passes
// for age.
TEST(LivestockCalibrationTest, TheAgeFactorIsWhatSeparatesTheModelFromDairyNz) {
  const double without_age = maintenance_me(500.0, 1.4, 11.0);

  AnimalClassParameters cow;
  cow.class_id = "mature_cow";
  cow.species_factor = 1.4;
  cow.sex_factor = 1.0;
  cow.standard_reference_weight_kg = 500.0;
  cow.grazing_coefficient = 0.0;
  cow.gain_energy_ceiling_mj_per_kg = 16.5;

  AnimalState six_years;
  six_years.liveweight_kg = 500.0;
  six_years.age_days = 2190.0;

  DietQuality diet;
  diet.metabolisable_energy_mj_per_kg_dm = 11.0;
  diet.digestibility_percent = 75.0;

  const double with_age = basal_net_energy_mj(cow, six_years) / diet.maintenance_efficiency();

  EXPECT_NEAR(without_age, 59.0, 1.0) << "DairyNZ give 59 at 500 kg";
  EXPECT_LT(with_age, 52.0) << "with the age factor the model is well below the published table";

  // At six years the exponential has already gone below the floor Freer et al.
  // put on it, so what applies is the floor itself rather than the curve:
  // exp(-0.00008 * 2190) is 0.8393 and the answer is 0.84. Every animal older
  // than about 2179 days gets the same discount, which is worth knowing before
  // reading anything into the age of a mature cow.
  EXPECT_DOUBLE_EQ(age_factor(2190.0), 0.84);
  EXPECT_GT(std::exp(-0.00008 * 2190.0), 0.83) << "and the curve is only just under it";
  EXPECT_LT(std::exp(-0.00008 * 2190.0), 0.84);

  GTEST_LOG_(INFO) << "500 kg cow: " << without_age << " MJ without the age factor, " << with_age
                   << " with it; DairyNZ publish 59";
}

// The model against Beef + Lamb NZ's Nicol and Brookes sheep maintenance table,
// which is the right comparator and was not the one used at first.
//
// This project spent a while recording a 26% hole in its own sheep maintenance
// against B+LNZ. It was comparing against the wrong table. Beef + Lamb publish
// two, from two frameworks, in two documents, with the same worked example
// worded identically and answered differently:
//
//   A guide to feed planning for sheep farmers, Appendix 1.2
//     Nicol and Brookes (2007) - 50 kg ewe at maintenance = 8 MJ ME/day
//   Making every mating count, Appendix 3.1 and Table 2.2
//     Geenty and Rattray (1987)  - 50 kg ewe = 10 MJ ME/day
//
// The equations here descend from Nicol and Brookes by way of the OVERSEER
// manual, so the first is the one they should be judged against. The 26% goes
// away - but the agreement is not clean, and the shape of what is left is more
// interesting than either number.
//
// The model is 1.9% below at 45 kg and 14.8% below at 60 kg, so it is not a flat
// offset either. Do not read an exponent out of that: the published steps are
// 1.0, 1.0, 1.0, 0.5, 0.5 MJ, rounded to the half-megajoule and kinking at
// 60 kg, and fitting a power law to six such points gives 1.24, 0.62 or 1.02
// depending which end you use.
//
// The likelier explanation has an equation behind it. The ME review quotes the
// Nicol and Brookes maintenance as
//   MEm = K.S.M (0.28 W^0.75 exp(-0.03A))/km + 0.1 MEp + MEgraze + Ecold
// and this figure carries neither MEgraze nor Ecold. Grazing cost scales with
// liveweight rather than W^0.75, which would open the gap as the animal gets
// heavier - which is the direction seen. Open item 12.
TEST(LivestockCalibrationTest, TheModelRunsBelowNicolAndBrookesAndDivergesWithWeight) {
  const test::CalibrationTable table(table_path("blnz_nicol_brookes_2007_ewe_me.csv"));

  int compared = 0;
  double worst = 0.0;
  double lightest = 0.0;
  double heaviest = 0.0;
  for (std::size_t row = 0; row < table.size(); ++row) {
    if (table.number(row, "liveweight_gain_g_per_day") != 0.0) {
      continue;
    }
    const double liveweight = table.number(row, "liveweight_kg");
    const double published = table.number(row, "me_mj_per_day");

    // 10.8 MJ ME/kg DM is the pasture the guide's own worked example uses.
    const double modelled = maintenance_me(liveweight, 1.0, 10.8);
    const double relative = std::abs(modelled - published) / published;
    worst = std::max(worst, relative);
    ++compared;

    EXPECT_LT(relative, 0.16) << liveweight << " kg: Nicol and Brookes " << published << ", model "
                              << modelled;
    if (liveweight == 45.0) {
      lightest = relative;
    }
    if (liveweight == 60.0) {
      heaviest = relative;
    }
  }

  ASSERT_EQ(compared, 6) << "Appendix 1.2 has six maintenance points";

  // The divergence is the finding, so it is asserted rather than left in a
  // comment. If a change ever makes these two agree evenly, this fails and
  // somebody has to say why.
  EXPECT_LT(lightest, 0.04) << "close at the light end";
  EXPECT_GT(heaviest, 0.10) << "and much further apart at 60 kg";

  // The published table is rounded and kinked rather than smooth, which is why
  // no exponent is asserted here. Recording the steps keeps that visible.
  EXPECT_DOUBLE_EQ(8.0 - 7.0, 1.0);
  EXPECT_DOUBLE_EQ(10.5 - 10.0, 0.5) << "the step halves at 60 kg";

  GTEST_LOG_(INFO) << "below Nicol and Brookes by " << (100.0 * lightest) << "% at 45 kg and "
                   << (100.0 * heaviest) << "% at 60 kg; worst " << (100.0 * worst) << "%";
}

// The two Beef + Lamb frameworks disagree with each other, and the model cannot
// match both. This records the shape of that disagreement so neither table can
// quietly be treated as a correction of the other.
//
// The remaining question is what the Geenty and Rattray practical grazing
// allowance is made of - it is a requirement for grazing adult sheep and may
// carry activity or margin the mechanistic model accounts for elsewhere or not
// at all. That needs the 1987 chapter, which has not been read. Open item 12.
TEST(LivestockCalibrationTest, TheTwoBeefAndLambFrameworksDisagreeWithEachOther) {
  const test::CalibrationTable nicol(table_path("blnz_nicol_brookes_2007_ewe_me.csv"));
  const test::CalibrationTable geenty(table_path("blnz_geenty_rattray_1987_ewe_me.csv"));

  // Both carry 50, 55 and 60 kg at maintenance.
  for (const double weight : {50.0, 55.0, 60.0}) {
    double from_nicol = 0.0;
    double from_geenty = 0.0;
    for (std::size_t row = 0; row < nicol.size(); ++row) {
      if (nicol.number(row, "liveweight_kg") == weight &&
          nicol.number(row, "liveweight_gain_g_per_day") == 0.0) {
        from_nicol = nicol.number(row, "me_mj_per_day");
      }
    }
    for (std::size_t row = 0; row < geenty.size(); ++row) {
      if (geenty.number(row, "liveweight_kg") == weight &&
          geenty.number(row, "liveweight_gain_g_per_day") == 0.0) {
        from_geenty = geenty.number(row, "me_mj_per_day");
      }
    }

    ASSERT_GT(from_nicol, 0.0) << weight << " kg missing from the Nicol and Brookes table";
    ASSERT_GT(from_geenty, 0.0) << weight << " kg missing from the Geenty and Rattray table";
    EXPECT_GT(from_geenty, from_nicol)
        << weight << " kg: Geenty and Rattray " << from_geenty << ", Nicol and Brookes "
        << from_nicol << " - the ordering between the two frameworks has changed";
  }
}

// The pregnancy table is monotonic, and this test exists because a version of
// it here was not.
//
// The same table appears twice in Making every mating count, and one of the two
// text extractions dropped a digit: 3.8 at two weeks before term came out as
// 0.8. That was recorded as an oddity of the source and defended with a test.
// It was an error of ours, and the test now checks the opposite thing.
TEST(LivestockCalibrationTest, ThePregnancyTableRisesTowardsTerm) {
  const test::CalibrationTable table(table_path("blnz_ewe_pregnancy_me.csv"));

  for (const double litter : {1.0, 2.0}) {
    double previous_weeks = 1e9;
    double previous_energy = -1.0;
    int seen = 0;
    for (std::size_t row = 0; row < table.size(); ++row) {
      if (table.number(row, "foetus_count") != litter) {
        continue;
      }
      const double weeks = table.number(row, "weeks_before_term");
      const double energy = table.number(row, "additional_me_mj_per_day");
      ASSERT_LT(weeks, previous_weeks) << "rows should run from 12 weeks down to term";
      EXPECT_GT(energy, previous_energy)
          << "litter " << litter << ", " << weeks
          << " weeks before term: energy fell as term approached, which is how the "
             "transcription error looked";
      previous_weeks = weeks;
      previous_energy = energy;
      ++seen;
    }
    EXPECT_EQ(seen, 6) << "six stages for litter size " << litter;
  }

  // The value that was wrong, asserted by name.
  for (std::size_t row = 0; row < table.size(); ++row) {
    if (table.number(row, "foetus_count") == 1.0 && table.number(row, "weeks_before_term") == 2.0) {
      EXPECT_DOUBLE_EQ(table.number(row, "additional_me_mj_per_day"), 3.8);
    }
  }
}

}  // namespace
}  // namespace paddock::core
