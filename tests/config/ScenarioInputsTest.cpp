// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

// The "what changed" view: what two scenarios were set up to do differently.
//
// **What is under test is whether a reader can trust the view.** A comparison
// header is read once, quickly, usually while somebody is talking, and it is
// believed - so the failures that matter are the quiet ones. A difference it
// does not notice becomes a result attributed to the wrong cause; a difference
// it invents sends somebody looking for a change nobody made; and an order that
// moves between runs makes a person hunt for the line they read last time.
//
// The bundles are built by hand from a loaded one rather than simulated. What
// is under test is the description and the comparison, and neither of them runs
// anything.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <sstream>
#include <string>
#include <vector>

#include <paddock/config/ScenarioInputs.hpp>

#include "../support/ShippedBundle.hpp"

namespace paddock::config {
namespace {

std::string bundle_path(const std::string& name) {
  return std::string(PADDOCK_DATA_DIR) + "/scenarios/" + name;
}

ScenarioBundle grazed() {
  return tests::load_on_flat_ground(bundle_path("canterbury-grazed"));
}

bool contains(const std::string& text, const std::string& needle) {
  return text.find(needle) != std::string::npos;
}

/// The change for one label, or nothing.
const InputChange* change_for(const InputComparison& comparison, const std::string& label) {
  const auto found =
      std::find_if(comparison.changes.begin(), comparison.changes.end(),
                   [&label](const InputChange& change) { return change.label == label; });
  return found == comparison.changes.end() ? nullptr : &*found;
}

core::IrrigationPolicy watered() {
  core::IrrigationPolicy rule;
  rule.enabled = true;
  rule.trigger_depletion_fraction = 0.5;
  rule.target_depletion_fraction = 0.15;
  rule.maximum_application_mm = 25.0;
  return rule;
}

// ---------------------------------------------------------------------------
// No differences.

// **Two identical scenarios must say so out loud.** A blank space under a
// heading reads as "nothing was checked", and the two cases are worlds apart:
// one means the comparison is sound, the other means it never ran.
TEST(ScenarioInputsTest, AScenarioComparedWithItselfHasNothingToReport) {
  const ScenarioBundle bundle = grazed();
  const InputComparison comparison = compare_inputs(bundle, bundle);

  EXPECT_TRUE(comparison.changes.empty());
  EXPECT_TRUE(comparison.changed_categories.empty());
  EXPECT_EQ(comparison.unchanged_categories, input_categories());
  EXPECT_FALSE(comparison.is_controlled()) << "nothing differing is not a controlled comparison";

  const std::string view = what_changed(comparison);
  EXPECT_TRUE(contains(view, "Nothing.")) << view;
  EXPECT_TRUE(contains(view, "configured identically")) << view;
}

// **And two different scenarios that really are configured identically.** The
// flagship demo's rain-fed half is Lincoln - that is the whole basis of the
// claim that the comparison sits on the farm this project validates against -
// and this is the view saying so about two separately written manifests rather
// than about one bundle compared with itself.
TEST(ScenarioInputsTest, TheRainFedDemoHalfIsConfiguredExactlyLikeLincoln) {
  const InputComparison comparison =
      compare_inputs(tests::load_on_flat_ground(bundle_path("lincoln-lurdf")),
                     tests::load_on_flat_ground(bundle_path("demo-irrigation-off")));

  EXPECT_TRUE(comparison.changes.empty()) << what_changed(comparison);
  EXPECT_TRUE(comparison.changed_categories.empty());
}

// ---------------------------------------------------------------------------
// One changed field.

TEST(ScenarioInputsTest, OneChangedFieldIsTheOnlyThingReported) {
  const ScenarioBundle before = grazed();
  ScenarioBundle after = before;
  // Taken out, changed and put back rather than written through the optional:
  // gtest's ASSERT_TRUE stops the test, and clang-tidy's optional analysis does
  // not model that. DataFilesTest.cpp:110 makes the same note.
  ASSERT_TRUE(after.management.has_value());
  core::ManagementPolicy policy = after.management.value_or(core::ManagementPolicy{});
  policy.minimum_cover_kg_dm_per_ha += 400.0;
  after.management = policy;

  const InputComparison comparison = compare_inputs(before, after);

  ASSERT_EQ(comparison.changes.size(), 1U);
  EXPECT_EQ(comparison.changes.front().category, "Grazing policy");
  EXPECT_EQ(comparison.changes.front().label, "do not graze below");
  EXPECT_TRUE(comparison.is_controlled());

  // The unit travels with the value. A comparison read aloud as "1600 to 2000"
  // is two numbers; "1600 kg DM/ha to 2000 kg DM/ha" is a decision.
  EXPECT_TRUE(contains(comparison.changes.front().before, "kg DM/ha"))
      << comparison.changes.front().before;
  EXPECT_TRUE(contains(comparison.changes.front().after, "kg DM/ha"))
      << comparison.changes.front().after;

  // Every other category is named as unchanged rather than left out, so a
  // reader can see that the weather was compared and found the same.
  EXPECT_EQ(comparison.changed_categories, std::vector<std::string>{"Grazing policy"});
  EXPECT_EQ(comparison.unchanged_categories.size(), input_categories().size() - 1U);

  const std::string view = what_changed(comparison);
  EXPECT_TRUE(contains(view, "One category differs")) << view;
  EXPECT_TRUE(contains(view, "Unchanged:")) << view;
  EXPECT_TRUE(contains(view, "Weather")) << view;
}

// ---------------------------------------------------------------------------
// Multiple irrigation fields.

// The flagship demo's shape: several settings change, all in one category, and
// the comparison is still controlled because they are one decision.
TEST(ScenarioInputsTest, SeveralIrrigationSettingsAreStillOneCategory) {
  const ScenarioBundle before = grazed();
  ScenarioBundle after = before;
  after.irrigation = watered();

  const InputComparison comparison = compare_inputs(before, after);

  EXPECT_EQ(comparison.changed_categories, std::vector<std::string>{"Irrigation"});
  EXPECT_TRUE(comparison.is_controlled());
  EXPECT_GE(comparison.changes.size(), 4U);
  for (const InputChange& change : comparison.changes) {
    EXPECT_EQ(change.category, "Irrigation") << change.label;
  }

  // **Rain-fed reads as one line, not as three settings nobody used.** A
  // scenario with irrigation off still has a trigger and a target sitting at
  // their defaults, and listing those would put spurious differences beside
  // the real one.
  const InputChange* on_or_off = change_for(comparison, "irrigation");
  ASSERT_NE(on_or_off, nullptr);
  EXPECT_EQ(on_or_off->before, "off");
  EXPECT_EQ(on_or_off->after, "on");

  // The trigger is shown as the water still in the soil, which is the end a
  // person uses - FarmletGrid::available_water_fraction says the same. A
  // depletion fraction of 0.5 is half the profile left.
  const InputChange* trigger = change_for(comparison, "trigger");
  ASSERT_NE(trigger, nullptr);
  EXPECT_EQ(trigger->before, "-") << "a rain-fed scenario has no trigger to report";
  EXPECT_EQ(trigger->after, "50% available water");

  const InputChange* target = change_for(comparison, "refill target");
  ASSERT_NE(target, nullptr);
  EXPECT_EQ(target->after, "85% available water");

  const InputChange* most = change_for(comparison, "most at once");
  ASSERT_NE(most, nullptr);
  EXPECT_EQ(most->after, "25 mm");
}

// ---------------------------------------------------------------------------
// Unexpected extra differences.

// **The whole point of the view.** Somebody sets up an irrigation comparison,
// changes the stock by accident, and reads the difference in production as the
// irrigation. The view has to say that two things moved, before the outputs.
TEST(ScenarioInputsTest, AnUnexpectedSecondDifferenceIsSurfacedNotHidden) {
  const ScenarioBundle before = grazed();
  ScenarioBundle after = before;
  after.irrigation = watered();
  ASSERT_FALSE(after.mobs.empty());
  after.mobs.front().head += 150;

  const InputComparison comparison = compare_inputs(before, after);

  EXPECT_FALSE(comparison.is_controlled());
  EXPECT_EQ(comparison.changed_categories, (std::vector<std::string>{"Stock", "Irrigation"}));

  const std::string view = what_changed(comparison);
  EXPECT_TRUE(contains(view, "2 categories differ, not one")) << view;
  EXPECT_TRUE(contains(view, "cannot attribute its result")) << view;
  // Both are named, so nobody has to count the sections to find out which.
  EXPECT_TRUE(contains(view, "Stock")) << view;
  EXPECT_TRUE(contains(view, "Irrigation")) << view;
}

// A setting present on one side only is a difference, not a silence. Adding a
// mob is the case that would otherwise vanish.
TEST(ScenarioInputsTest, AMobOnOneSideOnlyIsADifference) {
  const ScenarioBundle before = grazed();
  ScenarioBundle after = before;
  ASSERT_FALSE(after.mobs.empty());
  MobSpec extra = after.mobs.front();
  extra.name = "hoggets";
  after.mobs.push_back(extra);

  const InputComparison comparison = compare_inputs(before, after);
  const InputChange* added = change_for(comparison, "hoggets");
  ASSERT_NE(added, nullptr) << "a mob added to one scenario has to show up";
  EXPECT_EQ(added->before, "-");
  EXPECT_NE(added->after, "-");
}

// **The worst case still fits a terminal.** Two unrelated bundles differ in
// seven categories, and the line naming them is the one line a reader most
// needs and the one most likely to wrap badly.
TEST(ScenarioInputsTest, EvenTheWorstComparisonFitsEightyColumns) {
  const InputComparison comparison =
      compare_inputs(grazed(), tests::load_on_flat_ground(bundle_path("demo-irrigation-on")));
  ASSERT_GT(comparison.changed_categories.size(), 5U) << "this needs a badly confounded pair";

  std::istringstream view(what_changed(comparison));
  std::string line;
  while (std::getline(view, line)) {
    EXPECT_LE(line.size(), 80U) << line;
  }
}

// ---------------------------------------------------------------------------
// Stable display ordering.

// **The order is a property of the domain, not of the data.** A view that
// listed the biggest change first, or listed categories in the order they
// happened to differ, would move between two runs of the same comparison - and
// a person watching it twice would have to find each line again.
TEST(ScenarioInputsTest, CategoriesKeepTheirOrderWhicheverOnesChanged) {
  const ScenarioBundle before = grazed();

  ScenarioBundle after = before;
  after.irrigation = watered();
  ASSERT_TRUE(after.management.has_value());
  core::ManagementPolicy policy = after.management.value_or(core::ManagementPolicy{});
  policy.minimum_cover_kg_dm_per_ha += 400.0;
  after.management = policy;
  ASSERT_FALSE(after.mobs.empty());
  after.mobs.front().head += 150;
  after.latitude_degrees += 1.0;

  const InputComparison comparison = compare_inputs(before, after);

  // Run before Stock before Grazing policy before Irrigation, which is
  // input_categories() order and not the order they were edited in above.
  EXPECT_EQ(comparison.changed_categories,
            (std::vector<std::string>{"Run", "Stock", "Grazing policy", "Irrigation"}));

  // And the changes themselves follow the same order, so the block reads down
  // the page in the order its own summary line lists.
  std::vector<std::string> order;
  for (const InputChange& change : comparison.changes) {
    if (order.empty() || order.back() != change.category) {
      order.push_back(change.category);
    }
  }
  EXPECT_EQ(order, comparison.changed_categories);

  // Every category appears exactly once, in either list.
  std::vector<std::string> all = comparison.changed_categories;
  all.insert(all.end(), comparison.unchanged_categories.begin(),
             comparison.unchanged_categories.end());
  std::sort(all.begin(), all.end());
  std::vector<std::string> expected = input_categories();
  std::sort(expected.begin(), expected.end());
  EXPECT_EQ(all, expected);
}

// The same comparison twice is the same text. Anything read off an unordered
// container would pass the ordering test above and still fail this one.
TEST(ScenarioInputsTest, TheSameComparisonRendersTheSameTextEveryTime) {
  const ScenarioBundle before = grazed();
  ScenarioBundle after = before;
  after.irrigation = watered();

  EXPECT_EQ(what_changed(compare_inputs(before, after)),
            what_changed(compare_inputs(before, after)));
}

// Reversing the two scenarios reverses the values and nothing else. A view that
// only worked one way round would be a trap for anybody who typed the bundles
// in the other order.
TEST(ScenarioInputsTest, SwappingTheScenariosSwapsTheValuesAndKeepsTheOrder) {
  const ScenarioBundle rain_fed = grazed();
  ScenarioBundle irrigated = rain_fed;
  irrigated.irrigation = watered();

  const InputComparison forward = compare_inputs(rain_fed, irrigated);
  const InputComparison backward = compare_inputs(irrigated, rain_fed);

  ASSERT_EQ(forward.changes.size(), backward.changes.size());
  EXPECT_EQ(forward.changed_categories, backward.changed_categories);
  for (std::size_t i = 0; i < forward.changes.size(); ++i) {
    const InputChange& one = forward.changes[i];
    const InputChange& other = backward.changes[i];
    EXPECT_EQ(one.label, other.label);
    // The crossed comparison is the point: what the first run reported as the
    // old value is what the second reports as the new one.
    EXPECT_EQ(one.before, other.after);
    EXPECT_EQ(one.after, other.before);
  }
}

// ---------------------------------------------------------------------------
// What is deliberately not compared.

// **Metadata about the file is not an input to the farm.** The engine version
// and the description say nothing about what was modelled, and the scenario's
// own name is how the two halves are told apart - so comparing it would make
// every comparison report at least one difference and teach the reader to skip
// the section.
TEST(ScenarioInputsTest, NameDescriptionAndEngineVersionAreNotDifferences) {
  const ScenarioBundle before = grazed();
  ScenarioBundle after = before;
  after.name = "something else entirely";
  after.description = "and a different description";
  after.engine_version = "9.9.9";

  const InputComparison comparison = compare_inputs(before, after);
  EXPECT_TRUE(comparison.changes.empty()) << what_changed(comparison);

  // The names still head the view, because that is what they are for.
  const std::string view = what_changed(comparison);
  EXPECT_TRUE(contains(view, "something else entirely")) << view;
}

// The shipped flagship pair, through the real loader: the demonstration this
// view exists for has to come out saying one thing changed.
TEST(ScenarioInputsTest, TheFlagshipDemoReportsIrrigationAndNothingElse) {
  const InputComparison comparison =
      compare_inputs(tests::load_on_flat_ground(bundle_path("demo-irrigation-off")),
                     tests::load_on_flat_ground(bundle_path("demo-irrigation-on")));

  EXPECT_EQ(comparison.changed_categories, std::vector<std::string>{"Irrigation"});
  EXPECT_TRUE(comparison.is_controlled()) << what_changed(comparison);

  const std::string view = what_changed(comparison);
  EXPECT_TRUE(contains(view, "40% available water")) << view;
  EXPECT_TRUE(contains(view, "85% available water")) << view;
  EXPECT_TRUE(contains(view, "25 mm")) << view;
  EXPECT_TRUE(contains(view, "One category differs")) << view;

  // Compact enough to stand beside the numbers it explains.
  EXPECT_LT(std::count(view.begin(), view.end(), '\n'), 16)
      << "the demo view has grown past what fits on a slide:\n"
      << view;
}

}  // namespace
}  // namespace paddock::config
