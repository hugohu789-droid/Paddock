#include "MapWindow.hpp"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QWidget>
#include <algorithm>
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
};

FieldStyle style_of(MapWindow::Field field) {
  switch (field) {
    case MapWindow::Field::SoilWater:
      return {"Soil water", "Soil water (mm)", viz::Ramp::Viridis};
    case MapWindow::Field::WaterStress:
      return {"Water stress", "Water stress (1 = unstressed)", viz::Ramp::Viridis};
    case MapWindow::Field::LegumeFraction:
      return {"Legume fraction", "Legume share of green DM", viz::Ramp::Viridis};
    case MapWindow::Field::Cover:
    default:
      return {"Pasture cover", "Pasture cover (kg DM/ha)", viz::Ramp::PastureGreen};
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

  play_button_ = new QPushButton("Play", this);
  timeline_ = new QSlider(Qt::Horizontal, this);
  date_label_ = new QLabel(this);
  date_label_->setMinimumWidth(110);
  summary_label_ = new QLabel(this);

  auto* controls = new QHBoxLayout;
  controls->addWidget(field_box_);
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
  connect(play_button_, &QPushButton::clicked, this, &MapWindow::toggle_play);
  connect(timer_, &QTimer::timeout, this, &MapWindow::advance_frame);
  refresh();
  scene_.reset_camera();
  resize(1100, 760);
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
}

void MapWindow::refresh() {
  if (dates_.empty()) {
    return;
  }
  const auto day =
      static_cast<std::size_t>(std::clamp(current_day_, 0, static_cast<int>(dates_.size()) - 1));

  const std::vector<core::Raster<double>>* series = &cover_;
  switch (field_) {
    case Field::SoilWater:
      series = &soil_water_;
      break;
    case Field::WaterStress:
      series = &water_stress_;
      break;
    case Field::LegumeFraction:
      series = &legume_fraction_;
      break;
    case Field::Cover:
    default:
      series = &cover_;
      break;
  }

  const core::Raster<double>& raster = (*series)[day];
  const FieldStyle style = style_of(field_);

  // The scale is fixed over the whole run rather than to the day on screen. A
  // per-day scale would make every frame look the same and hide the season,
  // which is the one thing the timeline exists to show.
  double lowest = raster.values().front();
  double highest = lowest;
  for (const core::Raster<double>& frame : *series) {
    const std::pair<double, double> range = viz::ColourScale::range_of(frame);
    lowest = std::min(lowest, range.first);
    highest = std::max(highest, range.second);
  }

  scene_.show(raster, viz::ColourScale(style.ramp, lowest, highest), style.legend);
  date_label_->setText(QString::fromStdString(dates_[day]));
  summary_label_->setText(QString("Mean pasture cover %1 kg DM/ha over %2 cells")
                              .arg(mean_cover_[day], 0, 'f', 0)
                              .arg(raster.size()));
  view_->renderWindow()->Render();
}

void MapWindow::show_day(int day) {
  current_day_ = day;
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
