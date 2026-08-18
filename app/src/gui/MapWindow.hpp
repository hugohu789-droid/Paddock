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
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <paddock/config/ScenarioConfig.hpp>
#include <paddock/config/ScenarioRun.hpp>
#include <paddock/core/Farmer.hpp>
#include <paddock/core/Raster.hpp>
#include <paddock/viz/MapScene.hpp>

#include "SetupPanel.hpp"

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

  /// `data_directory` is the repository's `data/`, which the setup panel scans
  /// for the scenarios and species it offers. `bundle` is the one to open on,
  /// and it is passed already loaded so that a bundle which will not load fails
  /// on the command line rather than inside a window.
  MapWindow(const config::ScenarioBundle& bundle, std::string bundle_directory,
            std::string data_directory, QWidget* parent = nullptr);

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
  /// Runs what the setup panel is asking for, and shows it.
  void start_run();
  void open_report();
  void show_day(int day);
  void change_field(int field);
  void change_scale(int mode);
  void toggle_play();
  void advance_frame();

 private:
  /// Steps the bundle with its stock on it, under `policy`, keeping every day's
  /// rasters as it goes.
  void simulate_managed(const config::ScenarioBundle& bundle, const core::ManagementPolicy& policy);

  /// Steps the pasture alone, for a bundle that carries no stock. Both paths
  /// exist because `canterbury-baseline` has no mobs and is still worth looking
  /// at; only this one can be taken for such a bundle, and a managed run of it
  /// would have no mob to report on.
  void simulate_pasture_only(const config::ScenarioBundle& bundle);

  /// Empties the per-day series, so a second run does not append to the first.
  void clear_series();

  /// Whole-run colour ranges and the timeline, once a run has been captured.
  void adopt_series();

  void keep_day(const core::FarmletGrid& grid, const std::string& date);
  void refresh();
  [[nodiscard]] const std::vector<core::Raster<double>>& series_of(Field field) const;

  /// The whole-run range of a field, computed once when the run is loaded.
  ///
  /// Cached rather than recomputed each frame for two reasons: it is a scan of
  /// every day of the run, and a legend that is meant to be fixed must be
  /// provably fixed rather than recomputed to the same answer 366 times.
  [[nodiscard]] std::pair<double, double> whole_run_range(Field field) const;

  QVTKOpenGLNativeWidget* view_ = nullptr;
  SetupPanel* setup_ = nullptr;
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

  /// The last run, kept so the report can be written from the same numbers the
  /// map is drawing rather than from a second run that might differ.
  std::optional<config::RunSummary> last_run_;
  std::optional<config::ScenarioBundle> last_bundle_;
  core::ManagementPolicy last_policy_;
  bool last_run_had_stock_ = false;

  std::string data_directory_;

  int current_day_ = 0;
  Field field_ = Field::Cover;
  ScaleMode scale_mode_ = ScaleMode::WholeRun;
};

}  // namespace paddock::app
