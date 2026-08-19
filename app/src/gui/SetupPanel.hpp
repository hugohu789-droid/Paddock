// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#pragma once

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QString>
#include <QWidget>
#include <string>
#include <vector>

#include <paddock/config/ScenarioRun.hpp>
#include <paddock/config/SpeciesConfig.hpp>
#include <paddock/core/Farmer.hpp>

namespace paddock::app {

/// Set a run up, start it, and read what it came to.
///
/// The map view could already show a year of pasture, but only the year the
/// bundle on the command line described: changing the stocking rate meant
/// editing a TOML file and restarting. This is the panel that makes the thing
/// demonstrable - pick the farm, pick the stock, press Run.
///
/// It deliberately does not let everything be edited. A scenario bundle is a
/// reproducible object with hashed inputs, and a panel that let a user retype
/// the soil parameters would quietly destroy that. What it exposes is what a
/// farmer decides: how many, of what, and how hard to push the farm.
class SetupPanel : public QWidget {
  Q_OBJECT

 public:
  /// What the user has asked for. A null `species` means "leave the mob as the
  /// bundle defines it", which is the honest default: the bundle's own stock
  /// come with a starting weight and an age chosen for that scenario.
  ///
  /// The species is a pointer into this panel's own list rather than a copy,
  /// so it stays valid as long as the panel does - which is as long as the
  /// window - and a run started from the panel cannot outlive it.
  struct Choices {
    std::string scenario_directory;
    const config::SpeciesDefinition* species = nullptr;
    int head = 0;
    double liveweight_kg = 0.0;
    core::ManagementPolicy policy;

    /// The ground to run over. Flat is what every bundle ships as and what
    /// every baseline was recorded on; the others are invented surfaces, and
    /// the panel says so rather than offering them as places.
    config::TerrainSpec terrain;
  };

  /// Scans `data_directory` for scenario bundles and species definitions. A
  /// directory with neither leaves the panel empty and disabled rather than
  /// throwing: an application that cannot find its data should say so in the
  /// window, not fail to open one.
  explicit SetupPanel(const std::string& data_directory, QWidget* parent = nullptr);

  [[nodiscard]] Choices choices() const;

  /// Selects the bundle in `directory` if the panel found it, and takes its
  /// settings: the head count and opening weight of the stock it carries, and
  /// the rules its farmer works to when it names them.
  ///
  /// The panel opens showing what the bundle says, so pressing Run without
  /// touching anything reproduces the published scenario rather than whatever
  /// this form happened to default to. `policy` is null for a bundle that names
  /// none, and the panel keeps its own defaults.
  ///
  /// The window does this rather than the panel loading bundles itself: a
  /// scenario that fails to load is a thing the window has to report anyway,
  /// and having two places that can fail at it would mean two ways of saying so.
  void adopt_bundle(const std::string& directory, int head, double liveweight_kg,
                    const core::ManagementPolicy* policy = nullptr,
                    const core::AnimalClassParameters* animal = nullptr);

  /// Say whether the farm on screen brought its own measured ground.
  ///
  /// A bundle that names an elevation snapshot has its ground settled: a survey
  /// is not a preference, and MapWindow::start_run will not let the formulae in
  /// this list overwrite one. The control was still enabled and still offering
  /// them, and the caveat below still announced an invented surface while a
  /// LiDAR tile was on screen - a control that does nothing, explaining
  /// something that is not happening.
  void show_measured_ground(bool measured);

  /// Chooses the ground by its position in the list, for the screenshot path.
  /// A view of terrain that only a person clicking can reach is a view nothing
  /// can check.
  void select_ground(int index);

  /// Why this configuration cannot be run, or empty if it can.
  ///
  /// Only genuine contradictions are errors. A stocking rate that looks high is
  /// not one of them - finding out what a farm can carry is what the model is
  /// for, and a panel that refused to run it would be answering the question by
  /// declining to ask it.
  [[nodiscard]] QString problem() const;

  /// What is worth saying before the run rather than after: chiefly that the
  /// chosen stock rest on parameters that are not published, which decides
  /// whether any number out of the run may be quoted.
  [[nodiscard]] QString caveat() const;

  void set_running(bool running);

  /// Fills the results section. `has_stock` false means the bundle models
  /// pasture alone, so the stock lines are omitted rather than shown as zeroes.
  void show_results(const config::RunSummary& run, bool has_stock);

  void show_failure(const QString& message);

 signals:
  void runRequested();
  void reportRequested();

  /// A different farm was chosen, with the directory it lives in.
  ///
  /// Separate from runRequested because choosing a farm is not the same as
  /// pressing Run on this one: the new farm brings its own stock and its own
  /// rules, and re-running with the form still showing the last farm's numbers
  /// would put one farm's mob on another farm's ground. The window reloads the
  /// bundle and then runs it.
  ///
  /// Not emitted while adopt_bundle is filling the form, which sets this box
  /// itself and would otherwise start a run from inside the setting of it.
  void scenarioChanged(const QString& directory);

 private slots:
  void refresh_readiness();

 private:
  /// Whether the chosen scenario supplies its own measured ground.
  bool measured_ground_ = false;

  /// True while adopt_bundle is filling the form from a bundle, so that the
  /// boxes it sets do not read as a person choosing something.
  bool adopting_ = false;

 private slots:

 private:
  void populate(const std::string& data_directory);

  [[nodiscard]] const config::SpeciesDefinition* selected_species() const;

  QComboBox* scenario_box_ = nullptr;
  QComboBox* terrain_box_ = nullptr;
  QComboBox* species_box_ = nullptr;
  QSpinBox* head_box_ = nullptr;
  QDoubleSpinBox* liveweight_box_ = nullptr;
  QDoubleSpinBox* cover_floor_box_ = nullptr;
  QDoubleSpinBox* rotation_box_ = nullptr;
  QDoubleSpinBox* target_gain_box_ = nullptr;
  QSpinBox* graze_days_box_ = nullptr;
  QSpinBox* spell_days_box_ = nullptr;
  QDoubleSpinBox* supplement_me_box_ = nullptr;
  QComboBox* preference_box_ = nullptr;
  QComboBox* floor_purchase_box_ = nullptr;
  QCheckBox* may_buy_box_ = nullptr;
  QPushButton* run_button_ = nullptr;
  QPushButton* report_button_ = nullptr;
  QLabel* problem_label_ = nullptr;
  QLabel* results_label_ = nullptr;

  std::vector<config::SpeciesDefinition> species_;
};

}  // namespace paddock::app
