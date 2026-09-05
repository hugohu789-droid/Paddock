// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#pragma once

#include <string>
#include <utility>
#include <vector>

#include <paddock/config/ScenarioConfig.hpp>

/// What two scenarios were set up to do differently, read off the scenarios
/// themselves.
///
/// **A comparison is only worth reading if you know what was changed, and until
/// this existed nothing in the project could say.** `ScenarioComparison` has a
/// "What differs" header and it is filled by the caller - in practice by
/// `SetupPanel::describe`, which reads the values out of its own widgets. That
/// describes what the panel was showing, not what the model was given: it has
/// no weather, no soil, no sward, no dates and no seed in it, so two bundles
/// differing in any of those compare as identical set-ups with mysteriously
/// different results.
///
/// This reads the **resolved** bundle instead - after the files are loaded and
/// the hashes checked - so what it reports is what the run was actually given.
///
/// **It is not a diff framework and should not become one.** There is one
/// domain here, its categories are known, and a generic tree-differ would trade
/// the units and the wording for the ability to compare things this project
/// does not have. The whole of the mechanism is: describe a bundle as an
/// ordered list of labelled values, then walk two lists in step.
///
/// **Nothing here reads a result.** A difference inferred from output values is
/// a guess about a cause, and this is meant to be the thing you check the
/// outputs against.
namespace paddock::config {

/// The categories a scenario's inputs fall into, in the order they are shown.
///
/// **Fixed, and fixed here.** A view that ordered these by how much they
/// changed would move them around between runs, and a person watching a
/// comparison twice would have to find them again each time. The order runs
/// from what the run is, through the ground and the weather over it, to what
/// the farmer does about all of that.
///
/// Run, Farm, Ground, Weather, Soil, Pasture, Stock, Grazing policy, Irrigation,
/// Money and rules.
[[nodiscard]] const std::vector<std::string>& input_categories();

/// One resolved setting, as a person reads it.
struct InputSetting {
  /// One of `input_categories()`.
  std::string category;

  /// What it is called in the field rather than in the struct:
  /// "refill target", not "target_depletion_fraction".
  std::string label;

  /// The value with its unit in it - "25 mm", "417 head", "40% available
  /// water". **Units are part of the value, not a column**, because a
  /// comparison read aloud is read as a sentence and a bare 25 beside a bare 40
  /// is two numbers nobody can weigh against each other.
  std::string value;
};

/// Every setting in a bundle that the model actually reads, in display order.
///
/// **What is deliberately left out.** The engine version and the description
/// are metadata about the file rather than inputs to the farm; the scenario's
/// own name is how the two halves are told apart, so listing it would make
/// every comparison report at least one difference. File paths are left out and
/// their SHA-256s carried instead: the same soil reached as `../lincoln/soil.toml`
/// from one bundle and `soil.toml` from another is the same soil, and the hash
/// says so where the path does not. That also means a changed sward shows up as
/// a changed sward without this file having to know what is inside one.
[[nodiscard]] std::vector<InputSetting> scenario_inputs(const ScenarioBundle& bundle);

/// One setting that is not the same in both.
struct InputChange {
  std::string category;
  std::string label;

  /// The two values, or "-" where a scenario has no such setting at all.
  std::string before;
  std::string after;
};

/// What changed between two scenarios, and what did not.
struct InputComparison {
  std::string before_name;
  std::string after_name;

  /// In `input_categories()` order, and within a category in the order
  /// `scenario_inputs` lists them.
  std::vector<InputChange> changes;

  /// The categories those changes fall in, and the categories with no change in
  /// them. Both in `input_categories()` order, and between them they name every
  /// category - **so a reader can see that weather was compared and found the
  /// same, which is a different statement from weather not being mentioned.**
  std::vector<std::string> changed_categories;
  std::vector<std::string> unchanged_categories;

  /// True when exactly one category differs, which is the only case in which a
  /// difference in the results can be attributed to anything.
  [[nodiscard]] bool is_controlled() const noexcept { return changed_categories.size() == 1; }
};

[[nodiscard]] InputComparison compare_inputs(const ScenarioBundle& before,
                                             const ScenarioBundle& after);

/// The compact block a person reads before they look at any output.
///
/// Small on purpose: on the flagship demo it is a heading, four lines and a
/// list of what stayed put, which is what fits on a slide beside the numbers it
/// explains.
[[nodiscard]] std::string what_changed(const InputComparison& comparison);

}  // namespace paddock::config
