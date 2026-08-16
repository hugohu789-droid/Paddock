#include "MapWindow.hpp"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QWidget>
#include <algorithm>
#include <limits>
#include <string>
#include <utility>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkNew.h>
#include <vtkRenderWindow.h>

#include <paddock/core/FarmletGrid.hpp>
#include <paddock/core/Weather.hpp>

namespace paddock::app {

namespace {

/// Milliseconds between frames when playing. Roughly a fortnight a second at
/// daily steps, which is fast enough to see a season turn and slow enough to
/// watch a drought arrive.
constexpr int kFrameInterval = 30;

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

MapWindow::MapWindow(const config::ScenarioBundle& bundle, QWidget* parent) : QMainWindow(parent) {
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

  auto* controls = new QHBoxLayout;
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

  timer_ = new QTimer(this);
  timer_->setInterval(kFrameInterval);
  run_scenario(bundle);

  timeline_->setRange(0, static_cast<int>(dates_.empty() ? 0 : dates_.size() - 1));
  connect(timeline_, &QSlider::valueChanged, this, &MapWindow::show_day);
  connect(field_box_, &QComboBox::currentIndexChanged, this, &MapWindow::change_field);
  connect(scale_box_, &QComboBox::currentIndexChanged, this, &MapWindow::change_scale);
  connect(play_button_, &QPushButton::clicked, this, &MapWindow::toggle_play);
  connect(timer_, &QTimer::timeout, this, &MapWindow::advance_frame);
  // The exact extent goes in the title, where it can be read once. Axis labels
  // are for orientation; seven-digit coordinates repeated across the bottom of
  // a 1.2 km map are not.
  if (!cover_.empty()) {
    const core::GeoTransform& transform = cover_.front().transform();
    const double width = static_cast<double>(cover_.front().cols()) * transform.cell_size;
    const double height = static_cast<double>(cover_.front().rows()) * transform.cell_size;
    setWindowTitle(QString("Paddock - %1   |   %2-%3 E, %4-%5 N   |   %6 x %7 km, %8 ha")
                       .arg(QString::fromStdString(bundle.name))
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
  scene_.reset_camera();
  resize(1200, 800);
}

void MapWindow::run_scenario(const config::ScenarioBundle& bundle) {
  core::FarmletGrid grid = bundle.make_grid();
  const core::WeatherSeries weather = bundle.weather->fetch(bundle.range);

  const std::size_t days = weather.records.size();
  cover_.reserve(days);
  soil_water_.reserve(days);
  water_stress_.reserve(days);
  legume_fraction_.reserve(days);
  dates_.reserve(days);
  mean_cover_.reserve(days);

  for (const core::DailyWeather& day : weather.records) {
    grid.step(day);
    cover_.push_back(grid.cover_kg_dm());
    soil_water_.push_back(grid.soil_water_mm());
    water_stress_.push_back(grid.water_stress());
    legume_fraction_.push_back(grid.legume_fraction());
    dates_.push_back(day.date.to_iso_string());
    mean_cover_.push_back(grid.mean_cover_kg_dm());
  }

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

  scene_.show(raster, viz::ColourScale(style.ramp, lowest, highest), legend);
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
