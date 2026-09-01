// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

/// What this farm leaches, and what a regional council would make of it.
///
/// **The nitrogen cycle used to be open at the animal.** Nitrogen left with the
/// grazed dry matter and never came back, so a grazed farm ran its soil down
/// and the budget closed anyway - which is what an outflow with no matching
/// inflow does. It is also why nothing could leach: in New Zealand pastoral
/// farming the excreta a grazing animal returns, not fertiliser, is the primary
/// source of nitrate leaching, and this model returned none.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include <paddock/config/NitrogenReport.hpp>
#include <paddock/config/ScenarioConfig.hpp>
#include <paddock/config/ScenarioRun.hpp>
#include <paddock/core/SnapshotWeather.hpp>

#include "../support/ShippedBundle.hpp"
#include "../support/ValueOf.hpp"

namespace paddock::config {
namespace {

core::DietQuality pasture_diet() {
  core::DietQuality diet;
  diet.metabolisable_energy_mj_per_kg_dm = 10.5;
  diet.digestibility_percent = 75.0;
  return diet;
}

FarmBusiness a_business() {
  FarmBusiness business;
  business.prices.lamb_dollars_per_kg_carcass = 7.80;
  business.prices.wool_dollars_per_kg = 3.80;
  business.prices.cull_ewe_dollars_per_head = 90.0;
  business.opening_balance_dollars = 32'000.0;

  for (int age = 2; age <= 6; ++age) {
    core::AgeCohort cohort;
    cohort.birth_year = 2015 - age;
    cohort.age_years = age;
    cohort.mob.name = "ewes";
    cohort.mob.head = 83;
    cohort.mob.animal.class_id = "sheep_ewe";
    cohort.mob.animal.species_factor = 1.0;
    cohort.mob.animal.sex_factor = 1.0;
    cohort.mob.animal.standard_reference_weight_kg = 66.0;
    cohort.mob.animal.grazing_coefficient = 0.0025;
    cohort.mob.animal.gain_energy_ceiling_mj_per_kg = 20.3;
    cohort.mob.animal.gestation_length_days = 150.0;
    cohort.mob.animal.milk_fat_percent = 7.0;
    cohort.mob.animal.milk_protein_percent = 5.8;
    cohort.mob.animal.breed_effect = 0.01;
    cohort.mob.state.liveweight_kg = 55.0;
    cohort.mob.state.age_days = 365.0 * age;
    business.flock.add(std::move(cohort));
  }
  return business;
}

RunSummary year_of(int starting_year) {
  ScenarioBundle bundle =
      tests::load_on_flat_ground(std::string(PADDOCK_DATA_DIR) + "/scenarios/lincoln-lurdf");

  core::SnapshotWeatherSource::Options options;
  options.path = std::string(PADDOCK_DATA_DIR) + "/scenarios/lincoln-lurdf/weather-2015-2025.csv";
  options.dataset = "open-meteo";
  options.licence = "CC BY 4.0";
  bundle.weather = std::make_shared<core::SnapshotWeatherSource>(options);
  bundle.range =
      core::DateRange{core::Date{starting_year, 7, 1}, core::Date{starting_year + 1, 6, 30}};

  return run_managed_scenario(
      bundle, tests::value_of(bundle.management, "a [management] section"), pasture_diet(),
      std::to_string(starting_year) + "-" + std::to_string((starting_year + 1) % 100),
      a_business());
}

NitrogenRegulation the_rule() {
  return load_nitrogen_regulation(std::string(PADDOCK_DATA_DIR) +
                                  "/regulations/canterbury-nitrogen.toml");
}

// **The animal gives its nitrogen back.** Before this the budget balanced with
// the nitrogen simply gone.
TEST(NitrogenLeachingTest, TheStockReturnMostOfWhatTheyEat) {
  const RunSummary run = year_of(2019);

  double grazed_out = 0.0;
  double returned_in = 0.0;
  for (const core::ProcessEntry& entry : run.ledger.entries(core::Budget::Nitrogen)) {
    if (entry.process == "grazing_offtake") {
      grazed_out = entry.outflow;
    }
    if (entry.process == "excreta_returned") {
      returned_in = entry.inflow;
    }
  }

  ASSERT_GT(grazed_out, 0.0) << "the stock ate something";
  EXPECT_GT(returned_in, 0.0) << "and gave most of it back, or nothing can leach";

  // A grazing animal keeps roughly a tenth of the nitrogen it eats - as
  // liveweight and wool - and returns the rest within days. Here the return can
  // exceed the grazed offtake, because bought feed brings nitrogen through the
  // gate that was never grazed off this farm.
  EXPECT_GT(returned_in, grazed_out * 0.7)
      << "an animal that kept this much of its nitrogen would be growing faster than a sheep";
}

// **Against measured New Zealand rates.** Sheep and beef farms leach around
// 17 kg N/ha/yr nationally; Canterbury dryland is reckoned at about half the
// equivalent irrigated land use, and this farm is unirrigated and unfertilised.
// So an ordinary year should sit well under the national sheep-and-beef figure,
// and a very wet year should not.
TEST(NitrogenLeachingTest, LeachingSitsWhereAnUnfertilisedDrylandFarmShould) {
  const double dry = year_of(2015).nitrate_leached_total_kg_per_ha();
  const double ordinary = year_of(2019).nitrate_leached_total_kg_per_ha();
  const double wet = year_of(2024).nitrate_leached_total_kg_per_ha();

  EXPECT_GT(dry, 1.0) << "a grazed farm leaches something, even in a drought";
  EXPECT_LT(dry, 15.0)
      << "the driest year in ten should be comfortably under the Selwyn Waihora trigger";

  EXPECT_GT(ordinary, 3.0);
  EXPECT_LT(ordinary, 17.0)
      << "an ordinary year on unfertilised dryland should be under the national sheep and beef "
         "average, which includes fertilised and wetter country";

  // **Drainage is what carries it**, so the wettest year in ten leaches most.
  EXPECT_GT(wet, dry * 2.0)
      << "a year that drained five times as much should leach far more, or drainage is not "
         "driving this";
}

// The diagnostic that separates a wet year from a leaky farm.
TEST(NitrogenLeachingTest, LeachingPerMillimetreOfDrainageIsSteadierThanTheTotal) {
  const NitrogenYear dry = nitrogen_year(year_of(2015), "dry");
  const NitrogenYear wet = nitrogen_year(year_of(2024), "wet");

  ASSERT_GT(dry.drainage_mm, 0.0);
  ASSERT_GT(wet.drainage_mm, 0.0);

  // The totals differ by a factor of three or so; per millimetre they should be
  // much closer, because the farm did not change - the weather did.
  const double total_ratio = wet.leached_kg_n_per_ha / dry.leached_kg_n_per_ha;
  const double per_mm_ratio = wet.kg_n_per_mm_drainage() / dry.kg_n_per_mm_drainage();

  EXPECT_GT(total_ratio, 2.0);
  EXPECT_LT(per_mm_ratio, total_ratio)
      << "if the per-millimetre figure moved as much as the total, drainage would not be the "
         "thing doing the work";
}

// **The compliance report has to name its rule and its exclusions.** A number
// with neither is worse than none: a reader cannot tell what it is being
// compared against or what it leaves out.
TEST(NitrogenLeachingTest, TheComplianceReportQuotesTheRuleAndItsGaps) {
  const NitrogenRegulation rule = the_rule();
  EXPECT_EQ(rule.zone, "Selwyn Waihora");
  EXPECT_DOUBLE_EQ(rule.leaching_trigger_kg_n_per_ha_per_year.value, 15.0);
  EXPECT_TRUE(rule.leaching_trigger_kg_n_per_ha_per_year.is_evidence())
      << "a threshold a farm is judged against cannot be a placeholder";

  const std::string report =
      nitrogen_compliance_report(nitrogen_year(year_of(2019), "2019-20"), rule);

  EXPECT_NE(report.find("Selwyn Waihora"), std::string::npos) << "the zone the rule applies to";
  EXPECT_NE(report.find("Environment Canterbury"), std::string::npos) << "who set it";
  EXPECT_NE(report.find("does not count"), std::string::npos) << "and what the figure leaves out";
  EXPECT_NE(report.find("attenuation"), std::string::npos);
}

// **A wet year puts this farm over, and a dry one does not** - which is what
// makes the report worth printing. A model whose verdict never changed would be
// describing a farm rather than a rule.
TEST(NitrogenLeachingTest, TheVerdictChangesWithTheYear) {
  const NitrogenRegulation rule = the_rule();

  const NitrogenYear dry = nitrogen_year(year_of(2015), "2015-16");
  const NitrogenYear wet = nitrogen_year(year_of(2024), "2024-25");

  // **Under in a dry year, over in a wet one**, which is the distinction that
  // matters. Whether the dry year is comfortable or merely close moved when the
  // farm started finishing its lambs - five more months of stock on the ground
  // is five more months of excreta - and it is now close, which is a farm worth
  // reporting on rather than one that never has to think about it.
  EXPECT_NE(dry.standing(rule), NitrogenStanding::OverTheTrigger);
  EXPECT_EQ(wet.standing(rule), NitrogenStanding::OverTheTrigger);

  // **And both halves of the loss are reported.** OVERSEER puts inter-patch
  // leaching under 15% of a grazed block's, which is the check on the one
  // fitted parameter in this chain.
  EXPECT_GT(dry.leached_from_patches_kg_n_per_ha, 0.0);
  EXPECT_GT(dry.leached_between_patches_kg_n_per_ha, 0.0)
      << "leaving this out was a stated understatement, and it is no longer left out";
  EXPECT_LT(dry.inter_patch_share(), 0.15)
      << "if this drifts past OVERSEER's 15% the fit has stopped holding";

  const std::string wet_report = nitrogen_compliance_report(wet, rule);
  EXPECT_NE(wet_report.find("OVER THE TRIGGER"), std::string::npos);

  // **And it says what being over actually means.** The plan asks farms over
  // the trigger to reduce against their own baseline; it does not declare them
  // in breach, and a report that said so would be overstating the regulation.
  EXPECT_NE(wet_report.find("not a breach on its own"), std::string::npos);
}

// Several years side by side, which is the comparison one year cannot make.
TEST(NitrogenLeachingTest, TheYearByYearReportPutsDrainageBeforeTheTotal) {
  const NitrogenRegulation rule = the_rule();

  std::vector<NitrogenYear> years;
  for (const int start : {2015, 2019, 2024}) {
    years.push_back(nitrogen_year(year_of(start),
                                  std::to_string(start) + "-" + std::to_string((start + 1) % 100)));
  }

  const std::string report = nitrogen_years_report(years, rule);

  EXPECT_NE(report.find("per mm"), std::string::npos);
  EXPECT_NE(report.find("2015-16"), std::string::npos);
  EXPECT_NE(report.find("2024-25"), std::string::npos);
  EXPECT_NE(report.find("drainage is weather"), std::string::npos)
      << "the report has to say why the totals move, or a council reads management into weather";
}

}  // namespace
}  // namespace paddock::config
