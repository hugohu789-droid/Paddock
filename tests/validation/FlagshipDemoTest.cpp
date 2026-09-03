// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

// The flagship demonstration: one farm, one year, run rain-fed and irrigated.
//
// **What this suite protects is the claim, not the numbers.** The comparison is
// worth showing somebody only if the two runs really do differ in one thing, and
// that is not something a reader can check by eye - the manifests are two
// hundred lines each and the interesting difference is four of them. So it is
// checked here twice over: every setting either side is compared after loading,
// and the raw manifests are compared again as text with the irrigation section
// removed. The first catches a setting that means something different; the
// second catches a setting this file has never been taught about.
//
// **Nothing here asserts that irrigation produced a particular yield.** The demo
// exists to show what the model does, and a test that pinned the answer would
// turn a demonstration into a target - which is how a model stops being evidence
// and starts being decoration. Whether the response is the right size is
// WinchmoreSeasonalTest's question, and it asks it against measured trial data.

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#include <paddock/config/ScenarioReport.hpp>
#include <paddock/config/ScenarioRun.hpp>

#include "../support/ShippedBundle.hpp"

namespace paddock::config {
namespace {

/// The conservation suite's own tolerance. Same promise, same number.
constexpr double kTolerance = 1e-9;

std::string demo_path(const std::string& half) {
  return std::string(PADDOCK_DATA_DIR) + "/scenarios/demo-irrigation-" + half;
}

core::DietQuality pasture_diet() {
  core::DietQuality diet;
  diet.metabolisable_energy_mj_per_kg_dm = 10.5;
  diet.digestibility_percent = 75.0;
  return diet;
}

/// A path reduced to the file it actually names.
///
/// `weakly_canonical` resolves the `..` segments, so
/// `demo-irrigation-off/../../economics/canterbury-sheep.toml` and its opposite
/// number under `demo-irrigation-on` come out as one string - which is the
/// truth about them, and a plain string comparison would call them different.
std::string same_file_key(const std::string& path) {
  if (path.empty()) {
    return {};
  }
  std::error_code failed;
  const std::filesystem::path resolved = std::filesystem::weakly_canonical(path, failed);
  return failed ? path : resolved.generic_string();
}

/// Every setting in a bundle except the irrigation rule, one line each.
///
/// **A serialiser rather than a run of fifty EXPECT_EQs**, because a comparison
/// written as fifty assertions still passes when a fifty-first field is added
/// and nobody remembers to compare it. This produces text, the two texts are
/// compared whole, and a field that grows a difference shows up as a diff.
///
/// The name and the description are left out deliberately: the two halves have
/// to be called different things or no report could tell them apart. The soil,
/// sward, species, price and rule files are compared by the hashes the manifest
/// records, which compares every parameter inside them at once and does it
/// without this test having to know what those parameters are.
std::vector<std::string> settings_of(const ScenarioBundle& bundle) {
  std::vector<std::string> lines;
  std::ostringstream out;
  out << std::setprecision(17);

  const auto text = [&lines](const std::string& key, const std::string& value) {
    lines.push_back(key + " = " + value);
  };
  const auto number = [&lines, &out](const std::string& key, double value) {
    out.str({});
    out << value;
    lines.push_back(key + " = " + out.str());
  };

  text("engine_version", bundle.engine_version);
  number("master_seed", static_cast<double>(bundle.master_seed));
  text("start_date", bundle.range.first.to_iso_string());
  text("end_date", bundle.range.last.to_iso_string());
  number("latitude_degrees", bundle.latitude_degrees);

  number("initial.soil_water_mm", bundle.initial_state.soil_water_mm);
  number("initial.grass", bundle.initial_state.grass_kg_dm_per_ha);
  number("initial.legume", bundle.initial_state.legume_kg_dm_per_ha);
  number("initial.mineral_nitrogen", bundle.initial_state.soil_mineral_nitrogen_kg_per_ha);

  for (const BundleInput& input : bundle.inputs) {
    text("input " + input.relative_path, input.recorded_sha256);
  }

  text("has_grid", bundle.grid.has_value() ? "yes" : "no");
  if (bundle.grid.has_value()) {
    number("grid.cols", static_cast<double>(bundle.grid->cols));
    number("grid.rows", static_cast<double>(bundle.grid->rows));
    number("grid.cell_size_m", bundle.grid->cell_size_m);
    number("grid.available_water_west_mm", bundle.grid->available_water_west_mm);
    number("grid.available_water_east_mm", bundle.grid->available_water_east_mm);
    number("grid.origin_easting", bundle.grid->origin_easting);
    number("grid.origin_northing", bundle.grid->origin_northing);
    number("grid.paddock_hectares", bundle.grid->paddock_hectares);
  }

  number("terrain.kind", static_cast<double>(bundle.terrain.kind));
  text("terrain.elevation_path", bundle.terrain.elevation_path);
  text("terrain.elevation_sha256", bundle.terrain.elevation_sha256);

  // Resolved rather than compared as written: the loader makes these absolute
  // against the bundle directory, so the same file reached from the two halves
  // spells differently and is still the same file. What has to match is which
  // price book and which regional rule this farm is read against.
  text("economics_file", same_file_key(bundle.economics_path));
  text("regulation_file", same_file_key(bundle.regulation_path));

  text("has_management", bundle.management.has_value() ? "yes" : "no");
  if (bundle.management.has_value()) {
    const core::ManagementPolicy& policy = *bundle.management;
    number("management.minimum_cover", policy.minimum_cover_kg_dm_per_ha);
    number("management.rotation_cover", policy.rotation_cover_threshold_kg_dm_per_ha);
    number("management.target_gain", policy.target_liveweight_gain_kg_per_day);
    number("management.maximum_graze_days", policy.maximum_graze_days);
    number("management.minimum_spell_days", policy.minimum_spell_days);
    number("management.supplement_me", policy.supplement_me_mj_per_kg_dm);
    number("management.may_buy_feed", policy.may_buy_feed ? 1.0 : 0.0);
  }

  for (const MobSpec& mob : bundle.mobs) {
    const std::string key = "mob[" + mob.name + "]";
    number(key + ".head", static_cast<double>(mob.head));
    number(key + ".paddock", static_cast<double>(mob.paddock));
    number(key + ".liveweight_kg", mob.liveweight_kg);
    number(key + ".age_days", mob.age_days);
    number(key + ".grazes_ahead", mob.grazes_ahead ? 1.0 : 0.0);
  }

  for (const core::GrazingPeriod& period : bundle.grazing.periods()) {
    const std::string key = "period[" + period.name + "]";
    text(key + ".from", period.dates.first.to_iso_string());
    text(key + ".to", period.dates.last.to_iso_string());
    number(key + ".system", static_cast<double>(period.rule.system));
    number(key + ".maximum_graze_days", period.rule.maximum_graze_days);
    number(key + ".minimum_spell_days", period.rule.minimum_spell_days);
  }

  return lines;
}

std::string read_file(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  EXPECT_TRUE(file.is_open()) << "cannot open " << path;
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

/// The manifest with its comments, its two names and any irrigation section
/// taken out - which is the whole of what the two halves are allowed to differ
/// in.
std::string manifest_without_irrigation(const std::string& half) {
  std::istringstream source(read_file(demo_path(half) + "/scenario.toml"));
  std::ostringstream kept;
  std::string line;
  bool in_irrigation = false;
  while (std::getline(source, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    const std::size_t first = line.find_first_not_of(" \t");
    const std::string trimmed = first == std::string::npos ? std::string{} : line.substr(first);

    if (!trimmed.empty() && trimmed.front() == '[') {
      in_irrigation = trimmed.rfind("[irrigation]", 0) == 0;
    }
    if (in_irrigation || trimmed.empty() || trimmed.front() == '#') {
      continue;
    }
    // The one pair of settings that has to differ: a comparison whose halves
    // answer to the same name is not one anybody could read a report of.
    if (trimmed.rfind("name =", 0) == 0 || trimmed.rfind("description =", 0) == 0) {
      continue;
    }
    kept << trimmed << '\n';
  }
  return kept.str();
}

/// Whether `word` appears in `text` bounded by something that is not a letter.
///
/// **A substring search here is a false alarm waiting to happen**: "maintenance"
/// contains "nan" and every report has a maintenance line in it.
bool has_word(const std::string& text, const std::string& word) {
  const auto is_letter = [](char character) {
    return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z');
  };
  for (std::size_t at = text.find(word); at != std::string::npos;
       at = text.find(word, at + word.size())) {
    const bool letter_before = at > 0 && is_letter(text[at - 1]);
    const std::size_t after = at + word.size();
    const bool letter_after = after < text.size() && is_letter(text[after]);
    if (!letter_before && !letter_after) {
      return true;
    }
  }
  return false;
}

bool all_finite(const std::vector<double>& series) {
  return std::all_of(series.begin(), series.end(),
                     [](double value) { return std::isfinite(value); });
}

RunSummary run(const std::string& half) {
  return run_managed_scenario(tests::load_on_flat_ground(demo_path(half)), pasture_diet(), half);
}

// ---------------------------------------------------------------------------
// 1. The two halves differ only in irrigation.

// The settings that reach the model are the same on both sides, once the
// irrigation rule and the two names are set aside.
TEST(FlagshipDemoTest, TheTwoHalvesShareEverySettingButIrrigation) {
  const ScenarioBundle off = load_scenario(demo_path("off"));
  const ScenarioBundle on = load_scenario(demo_path("on"));
  EXPECT_EQ(settings_of(off), settings_of(on));
}

// The same again on the raw files, which catches a setting added to one
// manifest that the serialiser above has never been taught to compare.
TEST(FlagshipDemoTest, TheTwoManifestsAreTheSameTextOutsideTheIrrigationSection) {
  EXPECT_EQ(manifest_without_irrigation("off"), manifest_without_irrigation("on"));
}

// The difference is present, and it is the one intended. A pair that passed the
// two tests above by both being rain-fed would demonstrate nothing at all.
TEST(FlagshipDemoTest, OneHalfIrrigatesAndTheOtherDoesNot) {
  const ScenarioBundle off = load_scenario(demo_path("off"));
  const ScenarioBundle on = load_scenario(demo_path("on"));

  EXPECT_FALSE(off.irrigation.has_value()) << "the rain-fed half must name no irrigation rule";

  // value_or rather than a dereference: gtest's ASSERT_TRUE stops the test on
  // failure, but clang-tidy's optional analysis does not model that, and a
  // disabled default reads as a failure here rather than as a pass.
  ASSERT_TRUE(on.irrigation.has_value());
  const core::IrrigationPolicy watered = on.irrigation.value_or(core::IrrigationPolicy{});
  EXPECT_TRUE(watered.enabled);

  // FAO-56 Table 22 gives grazed pasture p = 0.6. Pinned because a demo that
  // waters on a made-up trigger is not comparing irrigation with dryland, it is
  // comparing dryland with a number somebody chose.
  EXPECT_DOUBLE_EQ(watered.trigger_depletion_fraction, 0.6);

  // No losses claimed: this project has no New Zealand application-efficiency
  // figure, so the system models none and says so rather than inventing one.
  EXPECT_DOUBLE_EQ(on.irrigation_system.application_efficiency, 1.0);
}

// ---------------------------------------------------------------------------
// 2. Both runs are deterministic.

TEST(FlagshipDemoTest, EachHalfRepeatsItselfExactly) {
  for (const std::string& half : {std::string("off"), std::string("on")}) {
    const RunSummary first = run(half);
    const RunSummary second = run(half);

    ASSERT_EQ(first.cover_kg_dm_per_ha.size(), second.cover_kg_dm_per_ha.size()) << half;

    // Bit-level, not to a tolerance. A demo that reproduces to three decimal
    // places is a demo with something unpinned inside it.
    for (std::size_t day = 0; day < first.cover_kg_dm_per_ha.size(); ++day) {
      ASSERT_EQ(first.cover_kg_dm_per_ha[day], second.cover_kg_dm_per_ha[day])
          << half << ", day " << day;
      ASSERT_EQ(first.growth_kg_dm_per_ha[day], second.growth_kg_dm_per_ha[day])
          << half << ", day " << day;
      ASSERT_EQ(first.irrigation_mm[day], second.irrigation_mm[day]) << half << ", day " << day;
      ASSERT_EQ(first.liveweight_kg[day], second.liveweight_kg[day]) << half << ", day " << day;
    }
    EXPECT_EQ(first.eaten_kg_dm, second.eaten_kg_dm) << half;
    EXPECT_EQ(first.irrigation.effective_mm, second.irrigation.effective_mm) << half;
    EXPECT_EQ(first.closing_head, second.closing_head) << half;
  }
}

// ---------------------------------------------------------------------------
// 3. The budgets still close.

// Irrigation adds a water inflow, and an inflow counted into the soil but not
// into the ledger is exactly the error a demonstration would carry into a
// presentation without anybody noticing. Same tolerance as the conservation
// suite, because this is the same promise.
TEST(FlagshipDemoTest, BothHalvesCloseTheirBudgets) {
  for (const std::string& half : {std::string("off"), std::string("on")}) {
    const RunSummary summary = run(half);
    EXPECT_TRUE(summary.ledger.closes(core::Budget::Water, summary.closing_water_mm, kTolerance))
        << half << "\n"
        << summary.ledger.report(core::Budget::Water, summary.closing_water_mm);
    EXPECT_TRUE(
        summary.ledger.closes(core::Budget::DryMatter, summary.closing_cover_kg_dm, kTolerance))
        << half << "\n"
        << summary.ledger.report(core::Budget::DryMatter, summary.closing_cover_kg_dm);
    EXPECT_TRUE(
        summary.ledger.closes(core::Budget::Nitrogen, summary.closing_nitrogen_kg, kTolerance))
        << half << "\n"
        << summary.ledger.report(core::Budget::Nitrogen, summary.closing_nitrogen_kg);
  }
}

// ---------------------------------------------------------------------------
// 4. Nothing in either run is NaN or infinite.

// **A NaN reaches a chart as a gap and a table as "nan", and neither says which
// day went wrong.** Every series a report or a view reads is checked here, so a
// bad day is caught with its number attached rather than found in a demo.
TEST(FlagshipDemoTest, NoDayOfEitherRunIsNaNOrInfinite) {
  for (const std::string& half : {std::string("off"), std::string("on")}) {
    const RunSummary summary = run(half);
    ASSERT_FALSE(summary.dates.empty()) << half;

    EXPECT_TRUE(all_finite(summary.cover_kg_dm_per_ha)) << half << " cover";
    EXPECT_TRUE(all_finite(summary.green_kg_dm_per_ha)) << half << " green";
    EXPECT_TRUE(all_finite(summary.growth_kg_dm_per_ha)) << half << " growth";
    EXPECT_TRUE(all_finite(summary.nitrate_leached_kg_per_ha)) << half << " leaching";
    EXPECT_TRUE(all_finite(summary.liveweight_kg)) << half << " liveweight";
    EXPECT_TRUE(all_finite(summary.irrigation_mm)) << half << " irrigation";
    EXPECT_TRUE(all_finite(summary.water_stress)) << half << " water stress";

    EXPECT_TRUE(std::isfinite(summary.eaten_kg_dm)) << half;
    EXPECT_TRUE(std::isfinite(summary.irrigation.effective_mm)) << half;
    EXPECT_TRUE(std::isfinite(summary.closing_cover_kg_dm)) << half;
    EXPECT_TRUE(std::isfinite(summary.closing_water_mm)) << half;
    EXPECT_TRUE(std::isfinite(summary.closing_nitrogen_kg)) << half;
    EXPECT_TRUE(std::isfinite(summary.nitrate_leached_total_kg_per_ha())) << half;

    // FAO-56 Eq. 84 is bounded, and a stress coefficient outside [0, 1] is a
    // bug a finite check on its own would let through.
    for (std::size_t day = 0; day < summary.water_stress.size(); ++day) {
      ASSERT_GE(summary.water_stress[day], 0.0) << half << ", day " << day;
      ASSERT_LE(summary.water_stress[day], 1.0) << half << ", day " << day;
    }
    for (std::size_t day = 0; day < summary.irrigation_mm.size(); ++day) {
      ASSERT_GE(summary.irrigation_mm[day], 0.0) << half << ", day " << day;
    }
  }
}

// ---------------------------------------------------------------------------
// 5. The report and the day-by-day snapshots come out.

TEST(FlagshipDemoTest, BothHalvesRenderAReport) {
  for (const std::string& half : {std::string("off"), std::string("on")}) {
    const ScenarioBundle bundle = tests::load_on_flat_ground(demo_path(half));
    const RunSummary summary = run_managed_scenario(bundle, pasture_diet(), half);

    ReportOptions options;
    options.farm_name = "Lincoln demonstration";
    const std::string report = render_report(bundle, summary, options);

    EXPECT_GT(report.size(), 500U) << half;

    // As a whole word. The first version of this looked for the substring and
    // failed on "maintenance", which is the report doing its job.
    EXPECT_FALSE(has_word(report, "nan")) << half << ": a NaN reached the report";
    EXPECT_FALSE(has_word(report, "-nan")) << half << ": a NaN reached the report";
    EXPECT_FALSE(has_word(report, "inf")) << half << ": an infinity reached the report";
    EXPECT_FALSE(has_word(report, "-inf")) << half << ": an infinity reached the report";
  }
}

// The map view draws from the day stream and not from the summary, so a demo
// that summarises correctly and streams nothing is still a demo nobody can
// watch.
TEST(FlagshipDemoTest, TheIrrigatedHalfStreamsAFiniteDayForEveryDay) {
  const ScenarioBundle bundle = tests::load_on_flat_ground(demo_path("on"));
  ASSERT_TRUE(bundle.management.has_value());
  ASSERT_TRUE(bundle.irrigation.has_value());

  int days = 0;
  int watered_days = 0;
  const DayObserver each_day = [&days, &watered_days](const core::Farm& farm, const core::FarmDay&,
                                                      const core::IrrigationSchedule& schedule) {
    ++days;
    ASSERT_TRUE(std::isfinite(schedule.last_mean_mm()));
    if (schedule.last_mean_mm() > 0.0) {
      ++watered_days;
    }
    // The raster the map view colours, cell by cell. One NaN in it is one hole
    // in the picture somebody is being shown.
    const core::Raster<double> cover = farm.grid().cover_kg_dm();
    ASSERT_TRUE(all_finite(cover.values())) << "a cell went NaN on day " << days;
  };

  const RunSummary summary = run_managed_scenario(
      bundle, bundle.management.value_or(core::ManagementPolicy{}), pasture_diet(), "on", each_day,
      bundle.irrigation.value_or(core::IrrigationPolicy{}), bundle.irrigation_system);

  EXPECT_EQ(days, static_cast<int>(summary.dates.size()));
  EXPECT_GT(watered_days, 0) << "the irrigated half never watered";
}

// ---------------------------------------------------------------------------
// 6. It runs from what is committed, with nothing generated first.

// **`load_scenario` is the checked loader**, so this passes only if every hash
// in both manifests matches the file it names and the engine version is one
// this build can reproduce. A clean checkout has exactly these files and
// nothing else, which is what "from a clean build" has to mean for a bundle.
TEST(FlagshipDemoTest, BothBundlesLoadFromWhatIsCommitted) {
  for (const std::string& half : {std::string("off"), std::string("on")}) {
    const ScenarioBundle bundle = load_scenario(demo_path(half));
    EXPECT_TRUE(bundle.inputs_unchanged()) << half;
    for (const BundleInput& input : bundle.changed_inputs()) {
      ADD_FAILURE() << half << ": " << input.relative_path
                    << " no longer hashes to what the manifest recorded";
    }

    // The ground is a fetched snapshot and deliberately not in the archive, so
    // the demo has to be watchable without it - and fetchable for somebody who
    // wants the hills. Both are asserted rather than assumed.
    EXPECT_FALSE(bundle.terrain.elevation_path.empty()) << half;
    EXPECT_TRUE(bundle.terrain.is_fetchable()) << half;
  }
}

}  // namespace
}  // namespace paddock::config
