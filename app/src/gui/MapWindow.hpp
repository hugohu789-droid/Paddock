// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

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
#include <utility>
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

  /// What the colour scale spans.
  ///
  /// Both are needed and neither is right on its own. Fixed over the run, the
  /// timeline shows the season but a single day's spatial pattern is squeezed
  /// into a fraction of the ramp - in this example scenario, a day with a real
  /// 328 kg DM/ha difference across the farm uses 12% of it. Fitted to the day,
  /// the pattern is plain but every frame looks alike and the season vanishes.
  /// A third option exists for fields that have a natural range. Legume
  /// fraction moves between 0.13 and 0.18 across a whole year; stretching that
  /// across the full colour ramp shows the pattern but tells a reader the farm
  /// is wildly variable when it is not. On a 0 to 1 scale the same map is
  /// almost one colour, which is the honest picture.
  enum class ScaleMode : int { WholeRun = 0, ThisDay, Natural };

  explicit MapWindow(const config::ScenarioBundle& bundle, QWidget* parent = nullptr);

  /// Renders one frame and returns. Used by the CI smoke test, which has no one
  /// to click anything.
  void render_once();

  [[nodiscard]] std::size_t day_count() const noexcept { return dates_.size(); }

  /// The day on which the selected field varies most across the farm.
  ///
  /// The view opens there rather than on day one. In a temperate pasture the
  /// first weeks of the farm year are genuinely uniform - a full profile makes
  /// differences in soil water capacity irrelevant until something draws it
  /// down - so opening on day one shows a correct, flat, and completely
  /// uninformative map.
  [[nodiscard]] int most_varied_day() const;

 private slots:
  void show_day(int day);
  void change_field(int field);
  void change_scale(int mode);
  void toggle_play();
  void advance_frame();

 private:
  void run_scenario(const config::ScenarioBundle& bundle);
  void refresh();
  [[nodiscard]] const std::vector<core::Raster<double>>& series_of(Field field) const;

  /// The whole-run range of a field, computed once when the run is loaded.
  ///
  /// Cached rather than recomputed each frame for two reasons: it is a scan of
  /// every day of the run, and a legend that is meant to be fixed must be
  /// provably fixed rather than recomputed to the same answer 366 times.
  [[nodiscard]] std::pair<double, double> whole_run_range(Field field) const;

  QVTKOpenGLNativeWidget* view_ = nullptr;
  QComboBox* field_box_ = nullptr;
  QComboBox* scale_box_ = nullptr;
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
  /// Indexed by Field.
  std::vector<std::pair<double, double>> whole_run_ranges_;
  std::vector<double> mean_cover_;

  int current_day_ = 0;
  Field field_ = Field::Cover;
  ScaleMode scale_mode_ = ScaleMode::WholeRun;
};

}  // namespace paddock::app
