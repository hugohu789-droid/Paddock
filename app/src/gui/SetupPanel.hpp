// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#pragma once

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QString>
#include <QWidget>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <paddock/config/ScenarioRun.hpp>
#include <paddock/config/SpeciesConfig.hpp>
#include <paddock/core/Farmer.hpp>
#include <paddock/core/Irrigation.hpp>

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

    /// What the stock get out of the grass. Starts as the bundle's, and only a
    /// researcher can move it.
    core::DietQuality diet;

    /// The ground to run over. Flat is what every bundle ships as and what
    /// every baseline was recorded on; the others are invented surfaces, and
    /// the panel says so rather than offering them as places.
    config::TerrainSpec terrain;

    /// When to water and how far to refill, and what the plant can deliver.
    /// Off by default, so a rain-fed run is what you get without asking.
    core::IrrigationPolicy irrigation;
    core::IrrigationSystem irrigation_system;
  };

  /// Scans `data_directory` for scenario bundles and species definitions. A
  /// directory with neither leaves the panel empty and disabled rather than
  /// throwing: an application that cannot find its data should say so in the
  /// window, not fail to open one.
  explicit SetupPanel(const std::string& data_directory, QWidget* parent = nullptr);

  /// Whether the panel holds something that could be run.
  [[nodiscard]] bool ready() const noexcept { return ready_; }

  /// Whether a run of it would have a report worth opening.
  [[nodiscard]] bool can_report() const noexcept { return can_report_; }

  [[nodiscard]] Choices choices() const;

  /// Puts the panel back the way `chosen` describes.
  ///
  /// The inverse of choices(), and it has to stay the inverse: a scenario
  /// selected from the list is loaded through here and run, so anything this
  /// does not restore would make the run differ from the one the comparison
  /// table reported. A test holds the round trip.
  void adopt_choices(const Choices& chosen);

  /// Puts another button on the row that carries Reset and Run.
  ///
  /// **One row of actions, not two.** The window's scenario buttons used to sit
  /// on a line of their own directly under this one, which read as two separate
  /// sets of controls when they are one: four things a person can do to what is
  /// on screen. The panel owns the row because the row is part of the panel; the
  /// window keeps its buttons and their meaning.
  void add_action(QPushButton* button);

  /// How the panel is set, as label and value, for a comparison's header.
  ///
  /// Written as a person reads it rather than as the model stores it - "below
  /// 50% of available water" and not a depletion fraction of 0.5 - because the
  /// header exists to tell somebody what they changed.
  [[nodiscard]] std::vector<std::pair<std::string, std::string>> describe() const;

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
  /// `irrigation` is the rule the bundle names, or null for a rain-fed one -
  /// and null switches the box off rather than leaving whatever was there.
  void adopt_bundle(const std::string& directory, int head, double liveweight_kg,
                    const core::DietQuality& diet, const QString& measured_against,
                    const core::ManagementPolicy* policy = nullptr,
                    const core::AnimalClassParameters* animal = nullptr,
                    const core::IrrigationPolicy* irrigation = nullptr,
                    const core::IrrigationSystem* irrigation_system = nullptr);

  /// Say whether the farm on screen brought its own measured ground.
  ///
  /// A bundle that names an elevation snapshot has its ground settled: a survey
  /// is not a preference, and MapWindow::start_run will not let the formulae in
  /// this list overwrite one. The control was still enabled and still offering
  /// them, and the caveat below still announced an invented surface while a
  /// LiDAR tile was on screen - a control that does nothing, explaining
  /// something that is not happening.
  void show_measured_ground(bool measured);

  /// What the bundle says about where its fences came from, shown with the
  /// other caveats. Empty for a bundle whose boundaries are a survey.
  void show_paddock_note(const QString& note);

  /// Chooses the ground by its position in the list, for the screenshot path.
  /// A view of terrain that only a person clicking can reach is a view nothing
  /// can check.
  void select_ground(int index);

  /// Turns irrigation on for the headless path.
  ///
  /// The panel is the only place a run can be told to irrigate, and a feature
  /// that only a person clicking can reach is a feature nothing can check.
  void select_irrigation(bool on);

  /// Why this configuration cannot be run, or empty if it can.
  ///
  /// Only genuine contradictions are errors. A stocking rate that looks high is
  /// not one of them - finding out what a farm can carry is what the model is
  /// for, and a panel that refused to run it would be answering the question by
  /// declining to ask it.
  [[nodiscard]] QString problem() const;

 private:
  /// The one way the irrigation rule can contradict itself: refilling to less
  /// than the level it starts at, which would leave every watering short of
  /// where the rule said it wanted the ground.
  [[nodiscard]] QString problem_with_irrigation() const;

 public:
  /// What is worth saying before the run rather than after: chiefly that the
  /// chosen stock rest on parameters that are not published, which decides
  /// whether any number out of the run may be quoted.
  [[nodiscard]] QString caveat() const;

  void set_running(bool running);

  /// Fills the results section. `has_stock` false means the bundle models
  /// pasture alone, so the stock lines are omitted rather than shown as zeroes.
  void show_results(const config::RunSummary& run, bool has_stock,
                    const core::IrrigationTally& irrigation = {}, double hectares = 0.0);

  void show_failure(const QString& message);

 public:
  /// Who is looking at this panel.
  ///
  /// **Three people open a farm simulator and want different controls.** A
  /// consultant wants stock, cover floors and irrigation - the things a farmer
  /// decides. A researcher wants the model's own parameters, each with the
  /// provenance the data files already carry. Somebody writing a consent report
  /// wants to know which rule this farm is measured against and nothing else.
  ///
  /// Serving all three from one flat list served none of them: seventeen
  /// controls out of about two hundred configurable values, all of them the
  /// farmer's, and no way to reach the rest without editing TOML. Worse, the
  /// two parameters the other two roles most needed - the regional rule and the
  /// diet quality - were hardcoded, because with no role owning them nobody
  /// noticed they had no home. See docs/validation/verify.md, E57 and E58.
  enum class Role {
    /// Did it feed the stock, and what did that cost.
    Farmer,
    /// Is this defensible, and how much of it rests on evidence.
    Researcher,
    /// Would this farm comply, and against whose rule.
    Compliance,
  };

  [[nodiscard]] Role role() const noexcept { return role_; }

  /// Moves the selector, which moves the rows with it.
  void choose_role(int role);

 signals:
  void runRequested();
  void reportRequested();

  /// The panel's readiness changed, so whatever drives it should look again.
  void readinessChanged();

  /// What the last run came to, as rich text.
  ///
  /// **Sent out rather than shown here.** The panel is where a run is set up;
  /// what it came to belongs beside the map it drew, with the rest of the
  /// readings. Keeping it in the panel meant the two halves of one answer sat
  /// at opposite corners of the window - and the panel had to share its height
  /// with the scenario list, so the box was usually one line tall.
  void resultsReady(const QString& text);

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

  /// Where this bundle's paddock boundaries came from, when they came from a
  /// subdivision rather than a survey.
  QString paddock_note_;

  /// True while adopt_bundle is filling the form from a bundle, so that the
  /// boxes it sets do not read as a person choosing something.
  bool adopting_ = false;

 private slots:

 private:
  void populate(const std::string& data_directory);

  [[nodiscard]] const config::SpeciesDefinition* selected_species() const;

  /// Shows only the rows the current role has a use for.
  void apply_role();

  QComboBox* role_box_ = nullptr;
  Role role_ = Role::Farmer;

  /// Rows only a researcher edits: the model's own parameters.
  std::vector<QWidget*> researcher_rows_;

  /// Rows only a farmer edits: the day-to-day decisions. A researcher still
  /// sees them, because a parameter study needs the management it was run
  /// under; a compliance reader does not, because none of it is theirs to set.
  std::vector<QWidget*> farmer_rows_;

  QComboBox* scenario_box_ = nullptr;
  QDoubleSpinBox* diet_me_box_ = nullptr;
  QLabel* measured_against_label_ = nullptr;

  /// Rows only a compliance reader needs: which rule this farm is read against.
  std::vector<QWidget*> compliance_rows_;
  QDoubleSpinBox* diet_digestibility_box_ = nullptr;
  QComboBox* terrain_box_ = nullptr;

  /// The rows each group hides until somebody asks for them.
  ///
  /// **Normal is what changes the answer; advanced is what most people leave
  /// alone.** A panel that shows every setting at once is a panel where the two
  /// that matter are as hard to find as the twenty that do not - and this one
  /// now has to share its side of the window with a list of scenarios.
  std::vector<QWidget*> advanced_rows_;
  std::vector<QPushButton*> advanced_buttons_;

  /// Builds a group whose advanced rows fold away, given the rows to hide.
  /// `form` must already hold every row; the ones named are the ones that go.
  void fold_away(QGroupBox* group, QFormLayout* form, const std::vector<QWidget*>& advanced);

  /// Irrigation, as few controls as the thing needs to be understood.
  ///
  /// The trigger and the target are put to a person as "how much water is
  /// left" rather than as depletion, because that is the way a farmer says it
  /// - a paddock is at 40% rather than 60% depleted - and the two are the same
  /// number read from opposite ends.
  QCheckBox* irrigate_box_ = nullptr;
  QDoubleSpinBox* irrigation_trigger_box_ = nullptr;
  QDoubleSpinBox* irrigation_target_box_ = nullptr;
  QDoubleSpinBox* irrigation_maximum_box_ = nullptr;
  QDoubleSpinBox* irrigation_efficiency_box_ = nullptr;
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
  /// Whether the panel describes something that can be run, and whether the
  /// run it describes would have a report worth opening.
  ///
  /// **State rather than a button, because the buttons moved.** Run and Report
  /// now live beside the scenario list, where a run is of the whole list rather
  /// than of whatever the form happens to show. The panel still knows whether
  /// what it holds is usable, and says so.
  /// Runs whatever the panel is showing, on its own.
  ///
  /// **Back in the panel, because that is what it acts on.** It ran the panel
  /// once, then moved out to the scenario list where it meant "run the list",
  /// and a person setting a farm up had nothing to press. The two are different
  /// actions on different things and now have a button each.
  QPushButton* run_button_ = nullptr;

  /// Puts the panel back the way the bundle published it.
  QPushButton* reset_button_ = nullptr;

  /// The row those buttons sit on, kept so the window can add its own.
  QHBoxLayout* actions_ = nullptr;

  /// Gives every button on that row the width of the widest of them.
  void even_actions();

  /// The settings as the bundle published them, taken the moment it was
  /// adopted.
  ///
  /// **"Default" means what this bundle says, not a figure invented here.** The
  /// panel opens showing the bundle's own stock, its own farmer's rules and its
  /// own ground, so that pressing Run without touching anything reproduces the
  /// published scenario. That is the state worth being able to get back to: a
  /// factory default would be somebody's guess, and returning to it would
  /// quietly replace a published run with an invented one.
  std::optional<Choices> as_published_;

  /// The same state written out as describe() gives it, so "has anything
  /// changed" is one string comparison rather than an operator== over a dozen
  /// model types that a new field could silently escape.
  std::vector<std::pair<std::string, std::string>> published_description_;

  /// Whether the panel now says something different from what it published.
  [[nodiscard]] bool differs_from_published() const;

  /// Lights the Reset button when there is something to undo, and puts it out
  /// when there is not. Called from wherever either could have changed: a
  /// widget being edited, a bundle being adopted, a run starting or finishing.
  void refresh_reset();

  /// Whether the model is stepping. Held because Reset is refused while it is,
  /// and the panel is asked to refresh from places that do not know.
  bool running_ = false;

  bool ready_ = false;
  bool can_report_ = false;
  QLabel* problem_label_ = nullptr;

  std::vector<config::SpeciesDefinition> species_;
};

}  // namespace paddock::app
