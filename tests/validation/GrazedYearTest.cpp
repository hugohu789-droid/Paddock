// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

// A whole year of the closed loop, from a bundle.
//
// Everything before this tested a piece. This runs the lot from a file: pasture
// grows on ninety-six hectares, five hundred ewes eat it, being short costs them
// weight, the farmer moves them, and the paddocks rest. The point is not a
// number - the inputs are placeholders and no figure out of this is a
// prediction - but that the loop is stable rather than running away in either
// direction, and that it is driven by a file rather than by code.
//
// The series it writes is the artefact: CI keeps it, so a change that alters
// how a farm behaves over a year is visible rather than buried.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <fstream>
#include <string>
#include <vector>

#include <paddock/config/ScenarioConfig.hpp>

#include "../support/ShippedBundle.hpp"

namespace paddock::config {
namespace {

std::string bundle_path() {
  return std::string(PADDOCK_DATA_DIR) + "/scenarios/canterbury-grazed";
}

struct YearOfGrazing {
  std::vector<double> cover_kg_dm_per_ha;
  std::vector<double> liveweight_kg;
  std::vector<int> paddock_of_mob;

  int days_short = 0;
  int moves = 0;
  int short_spells = 0;
  int grazings_extended = 0;
  double eaten_kg_dm = 0.0;

  core::BudgetLedger ledger;

  /// Captured before the farm goes out of scope, so the budgets can be closed
  /// against what the farm actually finished holding.
  double closing_cover_kg_dm = 0.0;
  double closing_nitrogen_kg = 0.0;
  double closing_water_mm = 0.0;
};

YearOfGrazing run_the_year(const ScenarioBundle& bundle) {
  core::Farm farm = bundle.make_farm();
  core::Farmer farmer = bundle.make_farmer();

  core::DietQuality diet;
  // PLACEHOLDER, like everything else here: a mid-range pasture. Real diet
  // quality varies with season and species composition, which this model does
  // not yet derive.
  diet.metabolisable_energy_mj_per_kg_dm = 10.5;
  diet.digestibility_percent = 75.0;

  YearOfGrazing year;
  farm.set_opening_stocks(year.ledger);

  const core::WeatherSeries weather = bundle.weather->fetch(bundle.range);
  for (const core::DailyWeather& day : weather.records) {
    const core::Farmer::Day decisions = farmer.decide(farm, day.date);
    year.moves += static_cast<int>(decisions.moves.size());
    year.short_spells += decisions.short_spells;
    year.grazings_extended += decisions.grazings_extended;

    const core::FarmDay farm_day = farm.step(day, diet, &year.ledger);
    if (farm_day.any_mob_feed_supply_limited) {
      ++year.days_short;
    }
    year.eaten_kg_dm += farm_day.total_eaten_kg_dm;

    year.cover_kg_dm_per_ha.push_back(farm.grid().mean_cover_kg_dm());
    year.liveweight_kg.push_back(farm.mobs().front().mob.state.liveweight_kg);
    year.paddock_of_mob.push_back(static_cast<int>(farm.mobs().front().paddock()));
  }

  year.closing_cover_kg_dm = farm.grid().mean_cover_kg_dm();
  year.closing_nitrogen_kg = farm.grid().mean_total_nitrogen_kg();
  year.closing_water_mm = farm.grid().mean_soil_water_mm();
  return year;
}

// The bundle loads, with its stock and its calendar, and its hashes still match.
TEST(GrazedYearTest, TheBundleLoadsWithItsStockAndItsCalendar) {
  const ScenarioBundle bundle = tests::load_on_flat_ground(bundle_path());

  EXPECT_EQ(bundle.name, "canterbury_grazed");
  EXPECT_TRUE(bundle.inputs_unchanged()) << "a bundle whose inputs moved is not this bundle";

  ASSERT_EQ(bundle.mobs.size(), 1U);
  EXPECT_EQ(bundle.mobs.front().head, 500);
  // The scenario starts with lighter ewes than the species describes, which is
  // the point of being able to set it.
  EXPECT_DOUBLE_EQ(bundle.mobs.front().liveweight_kg, 55.0);
  EXPECT_EQ(bundle.mobs.front().animal.class_id, "sheep_ewe");

  // Four periods, two systems, and rotation appearing three times at two
  // different spell lengths.
  ASSERT_EQ(bundle.grazing.periods().size(), 4U);
  EXPECT_TRUE(bundle.grazing.validation_error(bundle.range).empty())
      << bundle.grazing.validation_error(bundle.range);
}

// The whole loop, for a year, and the thing that matters: it is stable. Cover
// neither runs away nor collapses, and the stock are still alive at the end.
TEST(GrazedYearTest, AYearOfGrazingIsStable) {
  const ScenarioBundle bundle = tests::load_on_flat_ground(bundle_path());
  const YearOfGrazing year = run_the_year(bundle);

  ASSERT_EQ(year.cover_kg_dm_per_ha.size(), 366U) << "2023-07-01 to 2024-06-30 is a leap year span";
  ASSERT_GT(year.eaten_kg_dm, 0.0) << "nothing was eaten, so nothing was exercised";

  // Conservation still holds with a year of grazing, a farmer moving stock, and
  // a calendar switching systems underneath it all.
  constexpr double kConservationTolerance = 1e-9;
  EXPECT_TRUE(
      year.ledger.closes(core::Budget::DryMatter, year.closing_cover_kg_dm, kConservationTolerance))
      << year.ledger.report(core::Budget::DryMatter, year.closing_cover_kg_dm);
  EXPECT_TRUE(
      year.ledger.closes(core::Budget::Nitrogen, year.closing_nitrogen_kg, kConservationTolerance))
      << year.ledger.report(core::Budget::Nitrogen, year.closing_nitrogen_kg);
  EXPECT_TRUE(
      year.ledger.closes(core::Budget::Water, year.closing_water_mm, kConservationTolerance))
      << year.ledger.report(core::Budget::Water, year.closing_water_mm);

  // Stability, stated as bounds a reader can judge rather than a pin. Cover
  // stays inside the range a grazed sward physically can: never below the
  // residual the sward parameters set, and never at a level that would mean the
  // stock had stopped eating.
  double lowest = year.cover_kg_dm_per_ha.front();
  double highest = lowest;
  for (const double cover : year.cover_kg_dm_per_ha) {
    lowest = std::min(lowest, cover);
    highest = std::max(highest, cover);
  }

  // **The floor moved because the farm did** (verify.md, E80). This asserted
  // 1,000 kg DM/ha when the bundle ran a synthetic generator drawing 700-930 mm
  // and a sward with a placeholder radiation use efficiency of 1.5. On real
  // Selwyn weather and the sourced sward the year bottoms near 740, and that low
  // is not the stock eating it out: it is 769 at 400 head and 733 at 1,200, a
  // 36 kg spread across a threefold stocking range. The winter trough is
  // senescence, not grazing.
  EXPECT_GT(lowest, 600.0) << "cover fell to " << lowest << " kg DM/ha, below any residual";
  EXPECT_LT(highest, 12000.0) << "cover reached " << highest
                              << " kg DM/ha, which is more than a grazed sward carries";

  // The stock are still stock.
  const double final_weight = year.liveweight_kg.back();
  EXPECT_GT(final_weight, 30.0) << "the mob finished at " << final_weight << " kg";
  EXPECT_LT(final_weight, 100.0) << "the mob finished at " << final_weight << " kg";

  GTEST_LOG_(INFO) << "cover " << lowest << " to " << highest << " kg DM/ha; liveweight "
                   << year.liveweight_kg.front() << " to " << final_weight << " kg; "
                   << year.eaten_kg_dm << " kg DM eaten; " << year.moves << " moves, "
                   << year.short_spells << " short spells, " << year.days_short << " days short";
}

// The calendar drives the farm rather than decorating it: under set stocking
// the mob stays put, and it moves again when rotation resumes.
TEST(GrazedYearTest, TheCalendarActuallyChangesWhatHappens) {
  const ScenarioBundle bundle = tests::load_on_flat_ground(bundle_path());
  const YearOfGrazing year = run_the_year(bundle);

  // Lambing runs 20 August to 28 October, days 50 to 119 of a run starting
  // 1 July. The mob should sit on one paddock throughout.
  const std::size_t lambing_start = 51;
  const std::size_t lambing_end = 119;
  ASSERT_GT(year.paddock_of_mob.size(), lambing_end);

  const int during_lambing = year.paddock_of_mob[lambing_start];
  for (std::size_t day = lambing_start; day <= lambing_end; ++day) {
    ASSERT_EQ(year.paddock_of_mob[day], during_lambing)
        << "the mob moved on day " << day << ", during set stocking";
  }

  // And it moves again once rotation resumes.
  bool moved_after = false;
  for (std::size_t day = lambing_end + 1; day < year.paddock_of_mob.size(); ++day) {
    if (year.paddock_of_mob[day] != during_lambing) {
      moved_after = true;
      break;
    }
  }
  EXPECT_TRUE(moved_after) << "rotation resumed but the mob never moved again";
}

// The series, written for CI to keep. A year of a farm is not something a
// single assertion can judge, and a plot somebody looks at is worth more than a
// bound nobody reads.
TEST(GrazedYearTest, WritesTheYearForInspection) {
  const ScenarioBundle bundle = tests::load_on_flat_ground(bundle_path());
  const YearOfGrazing year = run_the_year(bundle);

  const std::string path = std::string(PADDOCK_VALIDATION_OUTPUT_DIR) + "/grazed-year.csv";
  std::ofstream out(path);
  ASSERT_TRUE(out) << "cannot write " << path;

  out << "day,cover_kg_dm_per_ha,liveweight_kg,paddock\n";
  for (std::size_t day = 0; day < year.cover_kg_dm_per_ha.size(); ++day) {
    out << day << ',' << year.cover_kg_dm_per_ha[day] << ',' << year.liveweight_kg[day] << ','
        << year.paddock_of_mob[day] << '\n';
  }
  out.close();

  GTEST_LOG_(INFO) << "wrote " << path;
}

}  // namespace
}  // namespace paddock::config
