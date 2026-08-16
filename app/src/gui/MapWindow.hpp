#pragma once

#include <QComboBox>
#include <QLabel>
#include <QMainWindow>
#include <QPushButton>
#include <QSlider>
#include <QTimer>
#include <QVTKOpenGLNativeWidget.h>
#include <cstddef>
#include <string>
#include <vector>

#include <paddock/config/ScenarioConfig.hpp>
#include <paddock/core/Raster.hpp>
#include <paddock/viz/MapScene.hpp>

namespace paddock::app {

/// The 2D map view: a field of the farm, a colour scale, and a timeline.
///
/// The whole run is stepped once when the window opens and every day's rasters
/// are kept, so dragging the timeline is instant and, more importantly, shows
/// the same numbers every time it is dragged. A view that re-simulated on
/// scrub could show a different farm on the way back.
class MapWindow : public QMainWindow {
  Q_OBJECT

 public:
  /// Which field the map is showing. Each carries its own units and its own
  /// colour ramp, because a stress coefficient and a pasture cover are not
  /// comparable quantities and should not share a scale.
  enum class Field : int { Cover = 0, SoilWater, WaterStress, LegumeFraction };

  explicit MapWindow(const config::ScenarioBundle& bundle, QWidget* parent = nullptr);

  /// Renders one frame and returns. Used by the CI smoke test, which has no one
  /// to click anything.
  void render_once();

  [[nodiscard]] std::size_t day_count() const noexcept { return dates_.size(); }

 private slots:
  void show_day(int day);
  void change_field(int field);
  void toggle_play();
  void advance_frame();

 private:
  void run_scenario(const config::ScenarioBundle& bundle);
  void refresh();

  QVTKOpenGLNativeWidget* view_ = nullptr;
  QComboBox* field_box_ = nullptr;
  QSlider* timeline_ = nullptr;
  QPushButton* play_button_ = nullptr;
  QLabel* date_label_ = nullptr;
  QLabel* summary_label_ = nullptr;
  QTimer* timer_ = nullptr;

  viz::MapScene scene_;

  /// One entry per simulated day, per field.
  std::vector<core::Raster<double>> cover_;
  std::vector<core::Raster<double>> soil_water_;
  std::vector<core::Raster<double>> water_stress_;
  std::vector<core::Raster<double>> legume_fraction_;
  std::vector<std::string> dates_;
  std::vector<double> mean_cover_;

  int current_day_ = 0;
  Field field_ = Field::Cover;
};

}  // namespace paddock::app
