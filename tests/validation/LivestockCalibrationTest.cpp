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
