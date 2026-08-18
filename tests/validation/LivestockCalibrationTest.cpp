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

// NOT a validation. The model does not reproduce Beef + Lamb NZ's sheep
// maintenance and this test records by how much, so that the distance is a
// measured quantity rather than something a reader has to rediscover.
//
// The gap is flat across the range, which is what says the two are measuring
// different things rather than one being noisy. Activity has been ruled out:
// every term the OVERSEER manual has closes about a twentieth of it. The cause
// is open item 12.
//
// The bounds below are wide enough that this passes while the cause is unknown,
// and tight enough that finding the cause - and closing the gap - will fail it,
// which is the point.
TEST(LivestockCalibrationTest, TheModelSitsWellBelowBeefAndLambSheepMaintenance) {
  const test::CalibrationTable table(table_path("blnz_mature_ewe_me.csv"));

  int compared = 0;
  double lowest_shortfall = 1.0;
  double highest_shortfall = 0.0;

  for (std::size_t row = 0; row < table.size(); ++row) {
    if (table.number(row, "liveweight_gain_g_per_day") != 0.0) {
      continue;  // the maintenance row only
    }
    const double liveweight = table.number(row, "liveweight_kg");
    const double published = table.number(row, "me_mj_per_day");

    // 10.8 MJ ME/kg DM is the pasture the source's own worked example uses.
    const double modelled = maintenance_me(liveweight, 1.0, 10.8);
    const double shortfall = (published - modelled) / published;

    EXPECT_GT(shortfall, 0.0) << liveweight << " kg: the model is no longer below B+LNZ";
    lowest_shortfall = std::min(lowest_shortfall, shortfall);
    highest_shortfall = std::max(highest_shortfall, shortfall);
    ++compared;
  }

  ASSERT_EQ(compared, 5) << "Appendix 3.1 has five maintenance points";

  // Flat, not scattered: under 5 points of spread across a 26% gap.
  EXPECT_LT(highest_shortfall - lowest_shortfall, 0.05)
      << "the gap has stopped being flat, which would change what it means";

  EXPECT_GT(lowest_shortfall, 0.20) << "the gap has narrowed; if that is real, update open item 12";
  EXPECT_LT(highest_shortfall, 0.32) << "the gap has widened";

  GTEST_LOG_(INFO) << "model below B+LNZ Appendix 3.1 by " << (100.0 * lowest_shortfall) << " to "
                   << (100.0 * highest_shortfall) << "% across 40-60 kg";
}

// The B+LNZ pregnancy table is non-monotonic at two weeks before term. It is
// preserved exactly, and this test exists so that a well-meaning tidy-up of the
// data file fails rather than passes.
TEST(LivestockCalibrationTest, ThePublishedPregnancyOddityIsPreserved) {
  const test::CalibrationTable table(table_path("blnz_ewe_pregnancy_me.csv"));

  double at_four_weeks = 0.0;
  double at_two_weeks = 0.0;
  for (std::size_t row = 0; row < table.size(); ++row) {
    const double weeks = table.number(row, "weeks_before_term");
    if (weeks == 4.0) {
      at_four_weeks = table.number(row, "additional_me_mj_per_day");
    }
    if (weeks == 2.0) {
      at_two_weeks = table.number(row, "additional_me_mj_per_day");
    }
  }

  EXPECT_DOUBLE_EQ(at_four_weeks, 2.6);
  EXPECT_DOUBLE_EQ(at_two_weeks, 0.8);
  EXPECT_LT(at_two_weeks, at_four_weeks)
      << "the published table is non-monotonic here and must stay that way until an "
         "erratum or a stronger source says otherwise";
}

}  // namespace
}  // namespace paddock::core
