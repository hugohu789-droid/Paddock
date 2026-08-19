// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#include "SetupPanel.hpp"

#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QStringList>
#include <QVBoxLayout>
#include <algorithm>
#include <exception>
#include <string>
#include <utility>

#include <paddock/config/Provenance.hpp>

namespace paddock::app {

namespace {

/// Defaults the panel opens with.
///
/// These are the numbers `tests/validation/ManagedFarmTest.cpp` runs its farmer
/// on, which is the only reason to prefer them to any other round number: they
/// are the ones the model is exercised at, so a demonstration opens on a
/// configuration that is known to behave rather than on one nobody has run.
constexpr double kDefaultCoverFloor = 1600.0;
constexpr double kDefaultTargetGain = 0.0;
constexpr double kDefaultRotationThreshold = 2200.0;
constexpr int kDefaultMaximumGrazeDays = 3;
constexpr int kDefaultMinimumSpellDays = 35;
constexpr double kDefaultSupplementMe = 10.0;

QString number(double value, int decimals = 0) {
  return QString::number(value, 'f', decimals);
}

}  // namespace

SetupPanel::SetupPanel(const std::string& data_directory, QWidget* parent) : QWidget(parent) {
  scenario_box_ = new QComboBox(this);
  species_box_ = new QComboBox(this);

  // Ground. Flat first, because it is what the bundles ship as and what their
  // baselines were recorded on, and because the alternatives are formulae
  // rather than places - the names say which way the invented hill faces, not
  // where it is.
  terrain_box_ = new QComboBox(this);
  terrain_box_->addItem("Flat", 0);
  terrain_box_->addItem("Slope facing north (invented)", 1);
  terrain_box_->addItem("Slope facing south (invented)", 2);
  terrain_box_->addItem("Rolling (invented)", 3);
  terrain_box_->setToolTip(
      "Every farm in this project ran flat until terrain was connected to the model, so the cost "
      "of walking a slope and the radiation a slope receives never applied. The surfaces offered "
      "here are formulae with known derivatives, not surveys: they show what slope does, not what "
      "any particular hill does.");

  head_box_ = new QSpinBox(this);
  // One animal is a farm with one animal on it; nought is not a stocking rate,
  // it is a different scenario. The upper bound is deliberately far above
  // anything sensible - see problem(), which does not treat a high stocking
  // rate as an error.
  head_box_->setRange(1, 100000);
  head_box_->setSingleStep(50);

  liveweight_box_ = new QDoubleSpinBox(this);
  liveweight_box_->setRange(1.0, 1200.0);
  liveweight_box_->setDecimals(1);
  liveweight_box_->setSuffix(" kg");

  cover_floor_box_ = new QDoubleSpinBox(this);
  cover_floor_box_->setRange(0.0, 6000.0);
  cover_floor_box_->setSingleStep(100.0);
  cover_floor_box_->setDecimals(0);
  cover_floor_box_->setSuffix(" kg DM/ha");
  cover_floor_box_->setValue(kDefaultCoverFloor);

  rotation_box_ = new QDoubleSpinBox(this);
  rotation_box_->setRange(0.0, 6000.0);
  rotation_box_->setSingleStep(100.0);
  rotation_box_->setDecimals(0);
  rotation_box_->setSuffix(" kg DM/ha");
  rotation_box_->setValue(kDefaultRotationThreshold);

  // What the stock are meant to be doing. Zero is holding weight, which is the
  // floor rather than the goal: stock are sold by the kilogram, so a farmer
  // feeding to no gain is feeding to lose money slowly. Negative is allowed
  // because wintering a dry ewe down is a real decision.
  target_gain_box_ = new QDoubleSpinBox(this);
  target_gain_box_->setRange(-0.5, 2.0);
  target_gain_box_->setDecimals(3);
  target_gain_box_->setSingleStep(0.025);
  target_gain_box_->setSuffix(" kg/head/day");
  target_gain_box_->setValue(kDefaultTargetGain);

  graze_days_box_ = new QSpinBox(this);
  graze_days_box_->setRange(1, 60);
  graze_days_box_->setSuffix(" days");
  graze_days_box_->setValue(kDefaultMaximumGrazeDays);
  graze_days_box_->setToolTip(
      "How long the mob stays on one paddock under rotation. Smith and Dawson (1976): do not "
      "graze a pasture for more than three days with the major grazing mob.");

  spell_days_box_ = new QSpinBox(this);
  spell_days_box_->setRange(1, 200);
  spell_days_box_->setSuffix(" days");
  spell_days_box_->setValue(kDefaultMinimumSpellDays);
  spell_days_box_->setToolTip(
      "The rest a paddock gets before it is grazed again. Whether the farm has enough paddocks to "
      "hold this is a different question, and the one that separates a rotation from a shuffle.");

  supplement_me_box_ = new QDoubleSpinBox(this);
  supplement_me_box_->setRange(1.0, 14.0);
  supplement_me_box_->setDecimals(1);
  supplement_me_box_->setSingleStep(0.5);
  supplement_me_box_->setSuffix(" MJ ME/kg DM");
  supplement_me_box_->setValue(kDefaultSupplementMe);
  supplement_me_box_->setToolTip(
      "The energy in the feed the farmer buys, which decides how much of it a given shortfall "
      "takes. Baleage and hay sit below pasture.");

  // Which system to run. The first is what the model did before there was a
  // choice; the last reads the scenario's own [[grazing_period]], which a
  // managed run ignored entirely until now.
  preference_box_ = new QComboBox(this);
  preference_box_->addItem("Rotate when the cover can afford it",
                           static_cast<int>(core::GrazingPreference::ByCover));
  preference_box_->addItem("Rotate wherever possible",
                           static_cast<int>(core::GrazingPreference::PreferRotation));
  preference_box_->addItem("Set stock all year",
                           static_cast<int>(core::GrazingPreference::AlwaysSetStock));
  preference_box_->addItem("Follow the scenario's calendar",
                           static_cast<int>(core::GrazingPreference::FollowCalendar));

  floor_purchase_box_ = new QComboBox(this);
  floor_purchase_box_->addItem("Buy the whole demand",
                               static_cast<int>(core::FloorPurchase::WholeDemand));
  floor_purchase_box_->addItem("Graze to the floor, buy the rest",
                               static_cast<int>(core::FloorPurchase::HoldAtFloor));
  floor_purchase_box_->setToolTip(
      "At the floor, how much of the mob's demand the farmer buys. Buying the lot asks the "
      "pasture for nothing, so cover climbs away from the floor. Grazing down to the line buys "
      "less feed and leaves less grass, which is the trade a farmer actually makes.");

  may_buy_box_ = new QCheckBox("Buy feed when the farm cannot carry the stock", this);
  may_buy_box_->setChecked(true);
  may_buy_box_->setToolTip(
      "With this off the farmer must protect the sward or the stock and cannot do both, and "
      "the stock are what gives. It is the counterfactual the bought-feed figure is measured "
      "against.");

  run_button_ = new QPushButton("Run", this);
  run_button_->setDefault(true);
  report_button_ = new QPushButton("Report", this);
  report_button_->setEnabled(false);

  problem_label_ = new QLabel(this);
  problem_label_->setWordWrap(true);
  results_label_ = new QLabel(this);
  results_label_->setWordWrap(true);
  results_label_->setTextFormat(Qt::RichText);

  auto* farm_group = new QGroupBox("Farm", this);
  auto* farm_form = new QFormLayout;
  farm_form->addRow("Scenario", scenario_box_);
  farm_form->addRow("Ground", terrain_box_);
  farm_group->setLayout(farm_form);

  auto* stock_group = new QGroupBox("Stock", this);
  auto* stock_form = new QFormLayout;
  stock_form->addRow("Class", species_box_);
  stock_form->addRow("Head", head_box_);
  stock_form->addRow("Opening weight", liveweight_box_);
  stock_group->setLayout(stock_form);

  auto* management_group = new QGroupBox("Management", this);
  auto* management_form = new QFormLayout;
  management_form->addRow("Grazing", preference_box_);
  management_form->addRow("Do not graze below", cover_floor_box_);
  management_form->addRow("Rotate above", rotation_box_);
  management_form->addRow("Graze a paddock for", graze_days_box_);
  management_form->addRow("Then rest it for", spell_days_box_);
  management_form->addRow("Target gain", target_gain_box_);
  management_form->addRow(may_buy_box_);
  management_form->addRow("At the floor", floor_purchase_box_);
  management_form->addRow("Bought feed energy", supplement_me_box_);
  management_group->setLayout(management_form);

  auto* buttons = new QVBoxLayout;
  buttons->addWidget(run_button_);
  buttons->addWidget(report_button_);

  auto* results_group = new QGroupBox("Results", this);
  auto* results_layout = new QVBoxLayout;
  results_layout->addWidget(results_label_);
  results_group->setLayout(results_layout);

  auto* layout = new QVBoxLayout;
  layout->addWidget(farm_group);
  layout->addWidget(stock_group);
  layout->addWidget(management_group);
  layout->addLayout(buttons);
  layout->addWidget(problem_label_);
  layout->addWidget(results_group);
  layout->addStretch(1);
  setLayout(layout);
  setMinimumWidth(340);

  populate(data_directory);

  connect(run_button_, &QPushButton::clicked, this, &SetupPanel::runRequested);
  connect(report_button_, &QPushButton::clicked, this, &SetupPanel::reportRequested);
  connect(cover_floor_box_, &QDoubleSpinBox::valueChanged, this, &SetupPanel::refresh_readiness);
  connect(rotation_box_, &QDoubleSpinBox::valueChanged, this, &SetupPanel::refresh_readiness);
  connect(species_box_, &QComboBox::currentIndexChanged, this, &SetupPanel::refresh_readiness);
  connect(scenario_box_, &QComboBox::currentIndexChanged, this, &SetupPanel::refresh_readiness);
  connect(terrain_box_, &QComboBox::currentIndexChanged, this, &SetupPanel::refresh_readiness);
  connect(graze_days_box_, &QSpinBox::valueChanged, this, &SetupPanel::refresh_readiness);
  connect(spell_days_box_, &QSpinBox::valueChanged, this, &SetupPanel::refresh_readiness);
  refresh_readiness();
}

void SetupPanel::populate(const std::string& data_directory) {
  const QDir root(QString::fromStdString(data_directory));

  const QDir scenarios(root.filePath("scenarios"));
  for (const QString& entry : scenarios.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
    const QDir bundle(scenarios.filePath(entry));
    if (!QFileInfo::exists(bundle.filePath("scenario.toml"))) {
      continue;
    }
    scenario_box_->addItem(entry, bundle.absolutePath());
  }

  // The bundle's own stock are the first option and the default. A species
  // picked here replaces them, which is a coarser thing than it looks: the
  // bundle chose its animals' age and weight for that scenario, and swapping
  // the class keeps the scenario's numbers rather than the species file's.
  species_box_->addItem("As the scenario defines", QString());
  try {
    species_ = config::load_species_directory(root.filePath("species").toStdString());
  } catch (const std::exception&) {
    // A missing or unreadable species directory is not fatal: every bundle
    // already carries its own stock. problem() reports it if the user asks for
    // a species anyway, which they cannot, because the list is empty.
    species_.clear();
  }
  std::sort(species_.begin(), species_.end(),
            [](const config::SpeciesDefinition& left, const config::SpeciesDefinition& right) {
              return left.display_name < right.display_name;
            });
  for (const config::SpeciesDefinition& species : species_) {
    species_box_->addItem(QString::fromStdString(species.display_name),
                          QString::fromStdString(species.name));
  }

  const bool have_scenario = scenario_box_->count() > 0;
  run_button_->setEnabled(have_scenario);
  if (!have_scenario) {
    problem_label_->setText(
        QString("No scenario bundles under %1. Point the application at the repository's data "
                "directory.")
            .arg(root.filePath("scenarios")));
  }
}

const config::SpeciesDefinition* SetupPanel::selected_species() const {
  const QString name = species_box_->currentData().toString();
  if (name.isEmpty()) {
    return nullptr;
  }
  const auto found = std::find_if(species_.begin(), species_.end(),
                                  [&name](const config::SpeciesDefinition& species) {
                                    return species.name == name.toStdString();
                                  });
  return found == species_.end() ? nullptr : &*found;
}

SetupPanel::Choices SetupPanel::choices() const {
  Choices chosen;
  chosen.scenario_directory = scenario_box_->currentData().toString().toStdString();
  chosen.species = selected_species();
  chosen.head = head_box_->value();
  chosen.liveweight_kg = liveweight_box_->value();

  chosen.policy.minimum_cover_kg_dm_per_ha = cover_floor_box_->value();
  chosen.policy.rotation_cover_threshold_kg_dm_per_ha = rotation_box_->value();
  chosen.policy.target_liveweight_gain_kg_per_day = target_gain_box_->value();
  chosen.policy.maximum_graze_days = graze_days_box_->value();
  chosen.policy.minimum_spell_days = spell_days_box_->value();
  chosen.policy.supplement_me_mj_per_kg_dm = supplement_me_box_->value();
  chosen.policy.may_buy_feed = may_buy_box_->isChecked();
  chosen.policy.preference =
      static_cast<core::GrazingPreference>(preference_box_->currentData().toInt());
  chosen.policy.floor_purchase =
      static_cast<core::FloorPurchase>(floor_purchase_box_->currentData().toInt());

  // Gradients as a rise per metre travelled. A tenth is a 10% grade, which is
  // 5.7 degrees - rolling rather than steep, and about as much as a farm bike
  // takes without thinking about it.
  switch (terrain_box_->currentData().toInt()) {
    case 1:
      chosen.terrain.kind = config::TerrainSpec::Kind::Synthetic;
      chosen.terrain.surface.gradient_east = 0.0;
      chosen.terrain.surface.gradient_north = -0.10;
      break;
    case 2:
      chosen.terrain.kind = config::TerrainSpec::Kind::Synthetic;
      chosen.terrain.surface.gradient_east = 0.0;
      chosen.terrain.surface.gradient_north = 0.10;
      break;
    case 3:
      chosen.terrain.kind = config::TerrainSpec::Kind::Synthetic;
      chosen.terrain.surface.gradient_east = -0.01;
      chosen.terrain.surface.gradient_north = -0.02;
      chosen.terrain.surface.undulation_amplitude_m = 8.0;
      chosen.terrain.surface.undulation_wavelength_m = 400.0;
      break;
    case 0:
    default:
      chosen.terrain.kind = config::TerrainSpec::Kind::Flat;
      break;
  }
  return chosen;
}

void SetupPanel::adopt_bundle(const std::string& directory, int head, double liveweight_kg,
                              const core::ManagementPolicy* policy) {
  const int index = scenario_box_->findData(QString::fromStdString(directory));
  if (index >= 0) {
    scenario_box_->setCurrentIndex(index);
  }
  if (head > 0) {
    head_box_->setValue(head);
  }
  if (liveweight_kg > 0.0) {
    liveweight_box_->setValue(liveweight_kg);
  }
  if (policy != nullptr) {
    cover_floor_box_->setValue(policy->minimum_cover_kg_dm_per_ha);
    rotation_box_->setValue(policy->rotation_cover_threshold_kg_dm_per_ha);
    target_gain_box_->setValue(policy->target_liveweight_gain_kg_per_day);
    graze_days_box_->setValue(policy->maximum_graze_days);
    spell_days_box_->setValue(policy->minimum_spell_days);
    supplement_me_box_->setValue(policy->supplement_me_mj_per_kg_dm);
    may_buy_box_->setChecked(policy->may_buy_feed);
    const int preference = preference_box_->findData(static_cast<int>(policy->preference));
    if (preference >= 0) {
      preference_box_->setCurrentIndex(preference);
    }
    const int floor = floor_purchase_box_->findData(static_cast<int>(policy->floor_purchase));
    if (floor >= 0) {
      floor_purchase_box_->setCurrentIndex(floor);
    }
  }
}

void SetupPanel::select_ground(int index) {
  if (index >= 0 && index < terrain_box_->count()) {
    terrain_box_->setCurrentIndex(index);
  }
}

QString SetupPanel::problem() const {
  if (scenario_box_->count() == 0) {
    return "There is no scenario to run.";
  }
  if (spell_days_box_->value() <= graze_days_box_->value()) {
    return QString(
               "A paddock is grazed for %1 days and rested for %2, so it would be grazed "
               "again before the rest was over. That is not a rotation.")
        .arg(graze_days_box_->value())
        .arg(spell_days_box_->value());
  }
  if (cover_floor_box_->value() >= rotation_box_->value()) {
    return QString(
               "The cover floor (%1) is at or above the rotation threshold (%2), so the "
               "farmer would be told to start rotating only once the sward is already below "
               "the cover being protected. Put the floor below the threshold.")
        .arg(number(cover_floor_box_->value()))
        .arg(number(rotation_box_->value()));
  }
  return {};
}

QString SetupPanel::caveat() const {
  // Both can apply at once, and one must not hide the other: unverified stock
  // on invented ground is two separate reasons not to quote a number.
  QStringList notes;

  if (const config::SpeciesDefinition* species = selected_species(); species != nullptr) {
    const config::Provenance weakest = species->weakest_status();
    if (weakest != config::Provenance::Direct && weakest != config::Provenance::Derived) {
      notes << QString(
                   "%1 rests on at least one parameter marked '%2'. The run will be "
                   "arithmetically sound and its absolute figures are not quotable.")
                   .arg(QString::fromStdString(species->display_name),
                        QString::fromStdString(config::to_string(weakest)));
    }
  }

  if (terrain_box_->currentData().toInt() != 0) {
    notes << "The ground is an invented surface. What it shows is what slope does, not what any "
             "particular hill does.";
  }

  return notes.join("\n\n");
}

void SetupPanel::refresh_readiness() {
  const QString blocking = problem();
  run_button_->setEnabled(blocking.isEmpty());

  if (!blocking.isEmpty()) {
    problem_label_->setStyleSheet("font-weight: bold;");
    problem_label_->setText(blocking);
    return;
  }
  problem_label_->setStyleSheet({});
  problem_label_->setText(caveat());

  if (const config::SpeciesDefinition* species = selected_species(); species != nullptr) {
    liveweight_box_->setValue(species->typical_liveweight_kg);
  }
}

void SetupPanel::set_running(bool running) {
  run_button_->setEnabled(!running && problem().isEmpty());
  run_button_->setText(running ? "Running..." : "Run");
  scenario_box_->setEnabled(!running);
  terrain_box_->setEnabled(!running);
  species_box_->setEnabled(!running);
  head_box_->setEnabled(!running);
  liveweight_box_->setEnabled(!running);
  cover_floor_box_->setEnabled(!running);
  rotation_box_->setEnabled(!running);
  target_gain_box_->setEnabled(!running);
  graze_days_box_->setEnabled(!running);
  spell_days_box_->setEnabled(!running);
  supplement_me_box_->setEnabled(!running);
  preference_box_->setEnabled(!running);
  floor_purchase_box_->setEnabled(!running);
  may_buy_box_->setEnabled(!running);
}

void SetupPanel::show_results(const config::RunSummary& run, bool has_stock) {
  QString text =
      QString("<b>%1 days</b> simulated.<br>").arg(static_cast<qulonglong>(run.dates.size()));
  text += QString("Cover %1 to %2, mean %3 kg DM/ha.<br>")
              .arg(number(run.lowest_cover_kg_dm_per_ha()),
                   number(run.highest_cover_kg_dm_per_ha()), number(run.mean_cover_kg_dm_per_ha()));

  if (has_stock) {
    text += QString("Liveweight %1 to %2 kg, a change of %3 kg.<br>")
                .arg(number(run.opening_liveweight_kg(), 1), number(run.closing_liveweight_kg(), 1),
                     number(run.liveweight_change_kg(), 2));
    // Days short first among the stock lines, because a closing weight held up
    // by a farm that ran out of feed is the number most likely to be misread.
    text += QString("Short of feed on <b>%1 days</b>.<br>").arg(run.days_short);
    text += QString("Bought %1 kg DM on %2 days.<br>")
                .arg(number(run.bought_feed_kg_dm()))
                .arg(run.days_feed_was_bought());
    text += QString("%1 mob shifts.<br>").arg(run.moves);
  } else {
    text += "This scenario has no stock on it, so there is nothing eating the pasture.<br>";
  }

  const bool closes = run.ledger.closes(core::Budget::DryMatter, run.closing_cover_kg_dm, 1e-9) &&
                      run.ledger.closes(core::Budget::Nitrogen, run.closing_nitrogen_kg, 1e-9) &&
                      run.ledger.closes(core::Budget::Water, run.closing_water_mm, 1e-9);
  text += closes ? "Dry matter, nitrogen and water all balance."
                 : "<b>A budget did not close.</b> Treat this run as unsound.";

  results_label_->setText(text);
  report_button_->setEnabled(has_stock);
  report_button_->setToolTip(has_stock
                                 ? QString()
                                 : QString("The report describes what a farmer did with stock, "
                                           "and this scenario has none."));
}

void SetupPanel::show_failure(const QString& message) {
  results_label_->setText(QString("<b>The run failed.</b><br>%1").arg(message.toHtmlEscaped()));
  report_button_->setEnabled(false);
}

}  // namespace paddock::app
