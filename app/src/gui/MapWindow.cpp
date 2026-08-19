// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#include "MapWindow.hpp"

#include <QApplication>
#include <QDockWidget>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QWidget>
#include <algorithm>
#include <cstddef>
#include <exception>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkNew.h>
#include <vtkPNGWriter.h>
#include <vtkRenderWindow.h>
#include <vtkWindowToImageFilter.h>

#include <paddock/config/ScenarioReport.hpp>
#include <paddock/core/FarmletGrid.hpp>
#include <paddock/core/Weather.hpp>

#include "../AttachElevation.hpp"
#include "ReportDialog.hpp"

namespace paddock::app {

namespace {

/// Milliseconds between frames when playing. Roughly a fortnight a second at
/// daily steps, which is fast enough to see a season turn and slow enough to
/// watch a drought arrive.
constexpr int kFrameInterval = 30;

/// The metabolisable energy and digestibility of the pasture on offer.
///
/// Fixed rather than exposed on the panel because this model does not track
/// diet quality through the season, and a box a user could set would promise a
/// precision that is not there. It is the pair the validation tests run at.
/// See docs/verify.md.
constexpr double kPastureMe = 10.5;
constexpr double kPastureDigestibility = 75.0;

/// What grazed_on returns for a day the run does not have, and for a run with
/// no stock in it. A reference has to refer to something.
const std::vector<std::size_t> kNothingGrazed;

struct FieldStyle {
  const char* label;
  const char* legend;
  viz::Ramp ramp;
  /// A range the quantity means something on regardless of this run. Fractions
  /// have one; a pasture cover or a soil water depth does not.
  bool has_natural_range;
  double natural_low;
  double natural_high;
};

FieldStyle style_of(MapWindow::Field field) {
  switch (field) {
    case MapWindow::Field::SoilWater:
      return {"Soil water", "Soil water (mm)", viz::Ramp::Viridis, false, 0.0, 0.0};
    case MapWindow::Field::WaterStress:
      return {"Water stress", "Water stress (1 = unstressed)", viz::Ramp::Viridis, true, 0.0, 1.0};
    case MapWindow::Field::LegumeFraction:
      return {"Legume fraction", "Legume share of green DM", viz::Ramp::Viridis, true, 0.0, 1.0};
    case MapWindow::Field::Cover:
    default:
      return {
          "Pasture cover", "Pasture cover (kg DM/ha)", viz::Ramp::PastureGreen, false, 0.0, 0.0};
  }
}

}  // namespace

MapWindow::MapWindow(const config::ScenarioBundle& bundle, const std::string& bundle_directory,
                     std::string data_directory, QWidget* parent)
    : QMainWindow(parent), data_directory_(std::move(data_directory)) {
  setWindowTitle(QString::fromStdString("Paddock - " + bundle.name));

  view_ = new QVTKOpenGLNativeWidget(this);
  vtkNew<vtkGenericOpenGLRenderWindow> window;
  view_->setRenderWindow(window);
  window->AddRenderer(scene_.renderer());

  field_box_ = new QComboBox(this);
  for (const MapWindow::Field field :
       {Field::Cover, Field::SoilWater, Field::WaterStress, Field::LegumeFraction}) {
    field_box_->addItem(style_of(field).label, static_cast<int>(field));
  }

  scale_box_ = new QComboBox(this);
  scale_box_->addItem("Scale: whole run", static_cast<int>(ScaleMode::WholeRun));
  scale_box_->addItem("Scale: this day", static_cast<int>(ScaleMode::ThisDay));
  scale_box_->addItem("Scale: 0 to 1 (fractions)", static_cast<int>(ScaleMode::Natural));

  play_button_ = new QPushButton("Play", this);
  timeline_ = new QSlider(Qt::Horizontal, this);
  date_label_ = new QLabel(this);
  date_label_->setMinimumWidth(200);
  summary_label_ = new QLabel(this);

  view_box_ = new QComboBox(this);
  view_box_->addItem("Flat map", 0);
  view_box_->addItem("Terrain", 1);

  height_box_ = new QComboBox(this);
  // The factor is in the label rather than applied quietly, because
  // exaggeration makes every slope look steeper than it is and a reader has to
  // be able to see which picture they are looking at.
  height_box_->addItem("Heights true to scale", 1);
  height_box_->addItem("Heights x2", 2);
  height_box_->addItem("Heights x5", 5);
  height_box_->setEnabled(false);

  auto* controls = new QHBoxLayout;
  controls->addWidget(view_box_);
  controls->addWidget(height_box_);
  controls->addWidget(field_box_);
  controls->addWidget(scale_box_);
  controls->addWidget(play_button_);
  controls->addWidget(timeline_, 1);
  controls->addWidget(date_label_);

  auto* layout = new QVBoxLayout;
  layout->addWidget(view_, 1);
  layout->addLayout(controls);
  layout->addWidget(summary_label_);

  auto* central = new QWidget(this);
  central->setLayout(layout);
  setCentralWidget(central);

  // The setup panel docks rather than opening as a dialog, so the map stays
  // visible while a run is being set up. A farm is chosen by looking at it.
  setup_ = new SetupPanel(data_directory_, this);
  auto* dock = new QDockWidget("Run a scenario", this);
  dock->setWidget(setup_);
  dock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
  addDockWidget(Qt::LeftDockWidgetArea, dock);

  timer_ = new QTimer(this);
  timer_->setInterval(kFrameInterval);

  connect(timeline_, &QSlider::valueChanged, this, &MapWindow::show_day);
  connect(field_box_, &QComboBox::currentIndexChanged, this, &MapWindow::change_field);
  connect(scale_box_, &QComboBox::currentIndexChanged, this, &MapWindow::change_scale);
  connect(play_button_, &QPushButton::clicked, this, &MapWindow::toggle_play);
  connect(timer_, &QTimer::timeout, this, &MapWindow::advance_frame);
  connect(view_box_, &QComboBox::currentIndexChanged, this, &MapWindow::change_view);
  connect(height_box_, &QComboBox::currentIndexChanged, this, &MapWindow::change_exaggeration);
  connect(setup_, &SetupPanel::runRequested, this, &MapWindow::start_run);
  connect(setup_, &SetupPanel::reportRequested, this, &MapWindow::open_report);

  // Open on the bundle named on the command line, configured as that bundle
  // configures itself. Pressing Run without touching anything therefore
  // reproduces the scenario as written, which is what makes the panel safe to
  // explore from: the user can always get back to the published run.
  const int head = bundle.mobs.empty() ? 0 : bundle.mobs.front().head;
  const double liveweight = bundle.mobs.empty() ? 0.0 : bundle.mobs.front().liveweight_kg;
  setup_->adopt_bundle(bundle_directory, head, liveweight,
                       bundle.management.has_value() ? &*bundle.management : nullptr);

  resize(1400, 860);
  start_run();
  scene_.reset_camera();
}

void MapWindow::start_run() {
  const SetupPanel::Choices choices = setup_->choices();
  if (choices.scenario_directory.empty()) {
    return;
  }

  timer_->stop();
  play_button_->setText("Play");
  setup_->set_running(true);
  QApplication::setOverrideCursor(Qt::WaitCursor);

  last_failure_.clear();
  try {
    config::ScenarioBundle bundle = config::load_scenario(choices.scenario_directory);
    attach_elevation(bundle, choices.scenario_directory);
    if (!bundle.grid.has_value()) {
      throw std::runtime_error("This scenario has no [grid] section, so there is no map to draw.");
    }

    // The panel edits the stock and nothing else. Everything the bundle hashes
    // - weather, soil, sward - is left exactly as loaded, so a run started here
    // is still the bundle's run with a different mob on it.
    if (!bundle.mobs.empty()) {
      config::MobSpec& mob = bundle.mobs.front();
      mob.head = choices.head;
      mob.liveweight_kg = choices.liveweight_kg;
      if (choices.species != nullptr) {
        mob.animal = choices.species->energy;
        mob.age_days = choices.species->typical_age_days;
      }
    }

    // The ground the run is over. Like the stock, it is a thing a farmer picks -
    // except when the bundle names a measured surface of its own, which is not
    // a preference to be overridden. The panel offers formulae; a snapshot is a
    // survey, and swapping one for the other would quietly replace the ground
    // with an invention.
    if (bundle.terrain.kind != config::TerrainSpec::Kind::Snapshot) {
      bundle.terrain = choices.terrain;
    }

    clear_series();
    // The ground this run is over, taken once. Empty for flat, which is what
    // disables the terrain view rather than drawing a plane and calling it
    // terrain.
    elevation_ = bundle.make_elevation();
    last_run_had_stock_ = !bundle.mobs.empty();
    if (last_run_had_stock_) {
      simulate_managed(bundle, choices.policy);
    } else {
      simulate_pasture_only(bundle);
    }

    last_policy_ = choices.policy;
    last_bundle_ = bundle;
    adopt_series();
    if (last_run_.has_value()) {
      setup_->show_results(*last_run_, last_run_had_stock_);
    }
  } catch (const std::exception& error) {
    // A failed run must not leave half a year on the timeline. Everything the
    // view draws from is cleared, so what is on screen is either a whole run or
    // nothing at all.
    clear_series();
    last_run_.reset();
    adopt_series();
    last_failure_ = error.what();
    setup_->show_failure(QString::fromUtf8(error.what()));
  }

  QApplication::restoreOverrideCursor();
  setup_->set_running(false);
}

void MapWindow::open_report() {
  if (!last_run_.has_value() || !last_bundle_.has_value() || !last_run_had_stock_) {
    return;
  }
  config::ReportOptions options;
  options.farm_name = last_bundle_->name;
  options.policy = &last_policy_;

  auto* dialog = new ReportDialog(
      QString::fromStdString(config::render_report(*last_bundle_, *last_run_, options)),
      QString::fromStdString(last_bundle_->name + "-report.md"), this);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->show();
}

std::optional<std::pair<double, double>> MapWindow::ground_range() const {
  if (!elevation_.has_value() || elevation_->empty()) {
    return std::nullopt;
  }
  double lowest = std::numeric_limits<double>::max();
  double highest = std::numeric_limits<double>::lowest();
  for (const double height : elevation_->values()) {
    lowest = std::min(lowest, height);
    highest = std::max(highest, height);
  }
  return std::make_pair(lowest, highest);
}

bool MapWindow::save_panel_screenshot(const std::string& path) {
  if (setup_ == nullptr) {
    return false;
  }
  return setup_->grab().save(QString::fromStdString(path), "PNG");
}

bool MapWindow::save_screenshot(const std::string& path) {
  vtkRenderWindow* window = view_->renderWindow();
  if (window == nullptr) {
    return false;
  }
  window->Render();

  vtkNew<vtkWindowToImageFilter> capture;
  capture->SetInput(window);
  // The back buffer, because the front one may already have been composited
  // with whatever is in front of the window.
  capture->ReadFrontBufferOff();
  capture->Update();

  vtkNew<vtkPNGWriter> writer;
  writer->SetFileName(path.c_str());
  writer->SetInputConnection(capture->GetOutputPort());
  writer->Write();
  return writer->GetErrorCode() == 0;
}

void MapWindow::show_configuration(int ground, bool terrain, int heights) {
  setup_->select_ground(ground);
  start_run();
  if (terrain) {
    view_box_->setCurrentIndex(1);
  }
  const int index = height_box_->findData(heights);
  if (index >= 0) {
    height_box_->setCurrentIndex(index);
  }
}

void MapWindow::change_view(int index) {
  const bool terrain = index == 1 && elevation_.has_value();
  if (terrain == showing_terrain_) {
    return;
  }
  showing_terrain_ = terrain;
  height_box_->setEnabled(terrain);

  vtkRenderWindow* window = view_->renderWindow();
  if (window != nullptr) {
    window->RemoveRenderer(showing_terrain_ ? scene_.renderer() : terrain_.renderer());
    window->AddRenderer(showing_terrain_ ? terrain_.renderer() : scene_.renderer());
  }
  refresh();
  if (showing_terrain_) {
    terrain_.reset_camera();
  } else {
    scene_.reset_camera();
  }
}

void MapWindow::change_exaggeration(int index) {
  terrain_.set_vertical_exaggeration(height_box_->itemData(index).toDouble());
  refresh();
  terrain_.reset_camera();
}

void MapWindow::clear_series() {
  cover_.clear();
  soil_water_.clear();
  water_stress_.clear();
  legume_fraction_.clear();
  dates_.clear();
  mean_cover_.clear();
  whole_run_ranges_.clear();
  boundaries_.clear();
  grazed_each_day_.clear();
  elevation_.reset();
}

const std::vector<std::size_t>& MapWindow::grazed_on(std::size_t day) const {
  return day < grazed_each_day_.size() ? grazed_each_day_[day] : kNothingGrazed;
}

void MapWindow::keep_day(const core::FarmletGrid& grid, const std::string& date) {
  cover_.push_back(grid.cover_kg_dm());
  soil_water_.push_back(grid.soil_water_mm());
  water_stress_.push_back(grid.water_stress());
  legume_fraction_.push_back(grid.legume_fraction());
  dates_.push_back(date);
  mean_cover_.push_back(grid.mean_cover_kg_dm());
}

void MapWindow::simulate_managed(const config::ScenarioBundle& bundle,
                                 const core::ManagementPolicy& policy) {
  core::DietQuality diet;
  diet.metabolisable_energy_mj_per_kg_dm = kPastureMe;
  diet.digestibility_percent = kPastureDigestibility;

  last_run_ = config::run_managed_scenario(
      bundle, policy, diet, bundle.name, [this](const core::Farm& farm, const core::FarmDay& day) {
        keep_day(farm.grid(), day.date.to_iso_string());

        // The fences do not move, so they are taken once, on the first day.
        if (boundaries_.empty()) {
          boundaries_.reserve(farm.paddocks().size());
          for (const core::Paddock& paddock : farm.paddocks()) {
            boundaries_.push_back(paddock.boundary);
          }
        }

        // Where the stock were. Taken from the farm rather than from the day's
        // MobDay, which carries one index per mob: a set stocked mob has the
        // run of every paddock, and only the farm knows the whole list.
        std::vector<std::size_t> grazed;
        for (const core::FarmMob& mob : farm.mobs()) {
          grazed.insert(grazed.end(), mob.paddocks.begin(), mob.paddocks.end());
        }
        std::sort(grazed.begin(), grazed.end());
        grazed.erase(std::unique(grazed.begin(), grazed.end()), grazed.end());
        grazed_each_day_.push_back(std::move(grazed));
      });
}

void MapWindow::simulate_pasture_only(const config::ScenarioBundle& bundle) {
  core::FarmletGrid grid = bundle.make_grid();
  const core::WeatherSeries weather = bundle.weather->fetch(bundle.range);

  config::RunSummary summary;
  summary.label = bundle.name;
  grid.set_opening_stocks(summary.ledger);

  const std::size_t days = weather.records.size();
  cover_.reserve(days);
  soil_water_.reserve(days);
  water_stress_.reserve(days);
  legume_fraction_.reserve(days);
  dates_.reserve(days);
  mean_cover_.reserve(days);

  for (const core::DailyWeather& day : weather.records) {
    grid.step(day, &summary.ledger);
    keep_day(grid, day.date.to_iso_string());
    summary.dates.push_back(day.date);
    summary.cover_kg_dm_per_ha.push_back(grid.mean_cover_kg_dm());
  }

  summary.closing_cover_kg_dm = grid.mean_cover_kg_dm();
  summary.closing_nitrogen_kg = grid.mean_total_nitrogen_kg();
  summary.closing_water_mm = grid.mean_soil_water_mm();
  last_run_ = std::move(summary);
}

void MapWindow::adopt_series() {
  for (const Field field :
       {Field::Cover, Field::SoilWater, Field::WaterStress, Field::LegumeFraction}) {
    double lowest = std::numeric_limits<double>::max();
    double highest = std::numeric_limits<double>::lowest();
    for (const core::Raster<double>& frame : series_of(field)) {
      const std::pair<double, double> range = viz::ColourScale::range_of(frame);
      lowest = std::min(lowest, range.first);
      highest = std::max(highest, range.second);
    }
    whole_run_ranges_.emplace_back(lowest, highest);
  }

  timeline_->setRange(0, static_cast<int>(dates_.empty() ? 0 : dates_.size() - 1));

  if (boundaries_.empty()) {
    scene_.clear_boundaries();
    terrain_.clear_boundaries();
  } else {
    scene_.set_boundaries(boundaries_);
    terrain_.set_boundaries(boundaries_);
  }

  const bool have_ground = elevation_.has_value();
  view_box_->setEnabled(have_ground);
  view_box_->setToolTip(have_ground
                            ? QString()
                            : QString("This run was over flat ground, so there is no terrain to "
                                      "draw. Choose a ground other than Flat and run again."));
  if (!have_ground && showing_terrain_) {
    view_box_->setCurrentIndex(0);
  }

  // The exact extent goes in the title, where it can be read once. Axis labels
  // are for orientation; seven-digit coordinates repeated across the bottom of
  // a 1.2 km map are not.
  if (!cover_.empty() && last_bundle_.has_value()) {
    const core::GeoTransform& transform = cover_.front().transform();
    const double width = static_cast<double>(cover_.front().cols()) * transform.cell_size;
    const double height = static_cast<double>(cover_.front().rows()) * transform.cell_size;
    setWindowTitle(QString("Paddock - %1   |   %2-%3 E, %4-%5 N   |   %6 x %7 km, %8 ha")
                       .arg(QString::fromStdString(last_bundle_->name))
                       .arg(transform.origin_easting, 0, 'f', 0)
                       .arg(transform.origin_easting + width, 0, 'f', 0)
                       .arg(transform.origin_northing - height, 0, 'f', 0)
                       .arg(transform.origin_northing, 0, 'f', 0)
                       .arg(width / 1000.0, 0, 'f', 2)
                       .arg(height / 1000.0, 0, 'f', 2)
                       .arg(width * height / 10000.0, 0, 'f', 0));
  }

  // Opening on the day the farm varies most, rather than on day one: see
  // most_varied_day().
  current_day_ = most_varied_day();
  timeline_->setValue(current_day_);
  refresh();
}

const std::vector<core::Raster<double>>& MapWindow::series_of(Field field) const {
  switch (field) {
    case Field::SoilWater:
      return soil_water_;
    case Field::WaterStress:
      return water_stress_;
    case Field::LegumeFraction:
      return legume_fraction_;
    case Field::Cover:
    default:
      return cover_;
  }
}

std::pair<double, double> MapWindow::whole_run_range(Field field) const {
  return whole_run_ranges_.at(static_cast<std::size_t>(field));
}

int MapWindow::most_varied_day() const {
  const std::vector<core::Raster<double>>& series = series_of(field_);
  int best_day = 0;
  double best_spread = -1.0;
  for (std::size_t day = 0; day < series.size(); ++day) {
    const std::pair<double, double> range = viz::ColourScale::range_of(series[day]);
    const double spread = range.second - range.first;
    if (spread > best_spread) {
      best_spread = spread;
      best_day = static_cast<int>(day);
    }
  }
  return best_day;
}

void MapWindow::refresh() {
  if (dates_.empty()) {
    return;
  }
  const auto day =
      static_cast<std::size_t>(std::clamp(current_day_, 0, static_cast<int>(dates_.size()) - 1));

  const std::vector<core::Raster<double>>& series = series_of(field_);
  const core::Raster<double>& raster = series[day];
  const FieldStyle style = style_of(field_);

  const std::pair<double, double> today = viz::ColourScale::range_of(raster);
  double lowest = today.first;
  double highest = today.second;
  // Which scale is actually in force, which is not always the one that was
  // asked for: pasture cover and soil water have no natural 0-to-1 range to
  // fall back on.
  ScaleMode applied = scale_mode_;
  if (scale_mode_ == ScaleMode::Natural && !style.has_natural_range) {
    applied = ScaleMode::WholeRun;
  }
  if (applied == ScaleMode::Natural) {
    lowest = style.natural_low;
    highest = style.natural_high;
  } else if (applied == ScaleMode::WholeRun) {
    const std::pair<double, double> range = whole_run_range(field_);
    lowest = range.first;
    highest = range.second;
  }

  // A legend that is rescaled per day means the same colour is a different
  // number on a different day, so the frames cannot be compared by eye. Saying
  // which scale is in force is the difference between a map and a decoration -
  // and it has to be the scale that was applied, not the one the box says. A
  // legend captioned "full 0 to 1 scale" over numbers running 1904 to 4563 is
  // worse than an unlabelled one.
  std::string legend = style.legend;
  switch (applied) {
    case ScaleMode::ThisDay:
      legend += "\nrescaled each day";
      break;
    case ScaleMode::Natural:
      legend += "\nfull 0 to 1 scale";
      break;
    case ScaleMode::WholeRun:
    default:
      legend += "\nfixed over the run";
      break;
  }

  const viz::ColourScale colours(style.ramp, lowest, highest);
  if (showing_terrain_ && elevation_.has_value()) {
    terrain_.show(raster, *elevation_, colours, legend);
    terrain_.show_grazed(grazed_on(day));
  } else {
    scene_.show(raster, colours, legend);
    scene_.show_grazed(grazed_on(day));
  }
  // The day number is here so that a playing timeline is visibly playing even
  // on a field whose colours barely move: legume fraction shifts by about a
  // ten-thousandth from one day to the next.
  date_label_->setText(QString("%1  (day %2 of %3)")
                           .arg(QString::fromStdString(dates_[day]))
                           .arg(day + 1)
                           .arg(dates_.size()));

  // The spread is on screen because a flat map is otherwise indistinguishable
  // from a broken one. Early in the farm year it is genuinely zero: a full soil
  // profile makes differences in water-holding capacity irrelevant until
  // something draws it down.
  const double spread = today.second - today.first;
  summary_label_->setText(
      QString("Mean pasture cover %1 kg DM/ha over %2 cells   |   %3 across the farm today: %4")
          .arg(mean_cover_[day], 0, 'f', 0)
          .arg(raster.size())
          .arg(style.label)
          .arg(spread > 0.0
                   ? QString("%1 to %2").arg(today.first, 0, 'f', 1).arg(today.second, 0, 'f', 1)
                   : QString("uniform (%1)").arg(today.first, 0, 'f', 1)));
  view_->renderWindow()->Render();
}

void MapWindow::show_day(int day) {
  current_day_ = day;
  refresh();
}

void MapWindow::change_scale(int mode) {
  scale_mode_ = static_cast<ScaleMode>(scale_box_->itemData(mode).toInt());
  refresh();
}

void MapWindow::change_field(int field) {
  field_ = static_cast<Field>(field_box_->itemData(field).toInt());
  refresh();
  scene_.reset_camera();
  view_->renderWindow()->Render();
}

void MapWindow::toggle_play() {
  if (timer_->isActive()) {
    timer_->stop();
    play_button_->setText("Play");
    return;
  }
  timer_->start();
  play_button_->setText("Pause");
}

void MapWindow::advance_frame() {
  if (dates_.empty()) {
    return;
  }
  const int next = (current_day_ + 1) % static_cast<int>(dates_.size());
  timeline_->setValue(next);
}

void MapWindow::render_once() {
  refresh();
  scene_.reset_camera();
  view_->renderWindow()->Render();
}

}  // namespace paddock::app
