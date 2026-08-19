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
#include <array>
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
#include <paddock/viz/TerrainScene.hpp>

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
  MapWindow(const config::ScenarioBundle& bundle, const std::string& bundle_directory,
            std::string data_directory, QWidget* parent = nullptr);

  /// Renders one frame and returns. Used by the CI smoke test, which has no one
  /// to click anything.
  void render_once();

  /// Sets up and runs one configuration without a person, for the screenshot
  /// path. `ground` indexes the panel's ground list; `terrain` asks for the
  /// three-dimensional view, which is ignored when the run was over flat
  /// ground because there would be nothing to draw.
  void show_configuration(int ground, bool terrain, int heights = 1);

  /// Why the last run failed, or empty if it did not.
  ///
  /// The panel shows this to a person. A headless run has no person, and used
  /// to report a failed run as "rendered 0 days" followed by VTK complaining
  /// about an empty pipeline - which says that something went wrong and
  /// nothing about what.
  [[nodiscard]] const std::string& last_failure() const noexcept { return last_failure_; }

  /// The elevation range of the ground the last run was over, in metres, or
  /// empty when it was over flat ground.
  ///
  /// Reported by the smoke run because a picture cannot show it. Ground drawn
  /// from a LiDAR snapshot and ground drawn from a formula look identical when
  /// both are nearly level, and two numbers separate them.
  [[nodiscard]] std::optional<std::pair<double, double>> ground_range() const;

  /// Why this run has no measured ground when its scenario names some, or
  /// empty. Exposed so that the headless path says it as well as the window:
  /// a screenshot of flat ground looks the same whether the farm is flat or
  /// the snapshot was never fetched.
  [[nodiscard]] const std::string& no_ground_reason() const noexcept { return no_ground_reason_; }

  /// Writes the setup panel to a PNG.
  ///
  /// The map has had a way to be looked at without a person since it was drawn,
  /// and the panel is just as much a deliverable: a form of nine rows can be
  /// perfectly wired and unreadable. Pure Qt widgets, so an ordinary grab
  /// captures them.
  bool save_panel_screenshot(const std::string& path);

  /// Writes the current frame to a PNG.
  ///
  /// A map is a claim about what the model did, and a claim nobody looks at is
  /// not checked. This is how the map gets checked without a person at the
  /// screen: render a day, write it out, look at it. Returns false if the file
  /// could not be written.
  bool save_screenshot(const std::string& path);

  [[nodiscard]] std::size_t day_count() const noexcept { return dates_.size(); }

  /// Open a different farm, exactly as choosing it in the panel would.
  ///
  /// Here for the headless path, which is the only way to exercise a change of
  /// farm without a person at the screen - and a change of farm is precisely
  /// where the camera used to be left behind, pointing at ground kilometres
  /// away from anything that was drawn.
  void open_scenario(const std::string& bundle_directory);

  /// Where the camera of whichever view is showing is pointing, in metres on
  /// the ground. Empty when there is no view yet.
  [[nodiscard]] std::optional<std::pair<double, double>> camera_focus() const;

  /// The extent of the farm currently drawn: easting, northing of the
  /// south-west corner, then width and height in metres.
  [[nodiscard]] std::optional<std::array<double, 4>> drawn_farm() const;

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
  void change_view(int index);
  void change_exaggeration(int index);
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

  /// Which paddocks had stock on them on `day`, or empty for a run without.
  [[nodiscard]] const std::vector<std::size_t>& grazed_on(std::size_t day) const;

  /// The stock as they stood on `day`, one marker per paddock a mob occupied.
  [[nodiscard]] const std::vector<viz::MobMarker>& mobs_on(std::size_t day) const;
  void refresh();

  /// Fills the line above the map with the day's weather.
  void show_weather(std::size_t day, double clearness);

 public:
  /// The same weather line in plain text, for the headless path.
  ///
  /// A screenshot captures the render window and not the labels around it, so
  /// without this the one part of the weather work a person can read is the
  /// part nothing can check.
  [[nodiscard]] const std::string& weather_line() const noexcept { return weather_line_; }

 private:
  [[nodiscard]] const std::vector<core::Raster<double>>& series_of(Field field) const;

  /// The whole-run range of a field, computed once when the run is loaded.
  ///
  /// Cached rather than recomputed each frame for two reasons: it is a scan of
  /// every day of the run, and a legend that is meant to be fixed must be
  /// provably fixed rather than recomputed to the same answer 366 times.
  [[nodiscard]] std::pair<double, double> whole_run_range(Field field) const;

  QVTKOpenGLNativeWidget* view_ = nullptr;
  SetupPanel* setup_ = nullptr;
  QComboBox* view_box_ = nullptr;
  QComboBox* height_box_ = nullptr;
  QComboBox* field_box_ = nullptr;
  QComboBox* scale_box_ = nullptr;
  QSlider* timeline_ = nullptr;
  QPushButton* play_button_ = nullptr;
  QLabel* date_label_ = nullptr;
  QLabel* summary_label_ = nullptr;

  /// The day's weather, above the map rather than below it: it is the thing
  /// driving what the map shows, so it reads first.
  QLabel* weather_label_ = nullptr;
  QTimer* timer_ = nullptr;

  viz::MapScene scene_;

  /// The same farm on the ground it sits on. Both scenes exist for the whole
  /// life of the window and only one is attached to the render window at a
  /// time, so switching between them does not rebuild a year of rasters.
  viz::TerrainScene terrain_;

  /// The elevation the run was over, or empty when it was over flat ground.
  ///
  /// Taken from the bundle rather than generated here. A view that made its own
  /// surface would be drawing a different farm from the one the numbers came
  /// from.
  std::optional<core::Raster<double>> elevation_;

  bool showing_terrain_ = false;

  /// Why this run has no measured ground, when it was supposed to. Empty when
  /// it has some, and when it never asked for any.
  std::string no_ground_reason_;

  /// A level surface for a run that has no elevation, kept because the scene
  /// holds a reference to what it is shown.
  core::Raster<double> flat_ground_;

  /// Where the last drawn farm was on the ground, so that a run over a
  /// different one can be recognised. Empty before the first run.
  ///
  /// Farms are kilometres apart - Lincoln and the Canterbury demonstration
  /// grid by about thirteen - so a camera left where the previous farm was
  /// shows blank space rather than the new one. Comparing extents rather than
  /// resetting on every run is what lets somebody zoom into a corner, change
  /// the stock, run again, and still be looking at that corner.
  std::optional<core::GeoTransform> drawn_extent_;
  std::size_t drawn_cols_ = 0;
  std::size_t drawn_rows_ = 0;

  /// Whether the farm on screen is somewhere other than the one that was there
  /// before, and the camera therefore has to move.
  [[nodiscard]] bool farm_moved(const core::Raster<double>& raster) const;

  /// One entry per simulated day, per field.
  std::vector<core::Raster<double>> cover_;
  std::vector<core::Raster<double>> soil_water_;
  std::vector<core::Raster<double>> water_stress_;
  std::vector<core::Raster<double>> legume_fraction_;
  std::vector<std::string> dates_;

  /// The weather each day was run on, kept so the view can say what the day
  /// was like and light the ground with that day's sun.
  std::vector<core::DailyWeather> weather_;

  /// The latitude the run was over, which the sun's position depends on.
  double latitude_degrees_ = 0.0;

  /// The hour the sun is drawn at, in SOLAR time.
  ///
  /// The model has no hours - a weather record is a day - so one hour has to
  /// stand for the day. Two in the afternoon is above the horizon on every day
  /// of a New Zealand year and sits north-west rather than due north, which is
  /// what gives the ground a lit side and a shaded one; solar noon puts the sun
  /// exactly north and flattens the relief. Solar time and not the clock:
  /// Lincoln's solar noon is about half an hour after 12:00 NZST.
  static constexpr double kSolarHourShown = 14.0;

  /// The weather line as plain text, kept as it is built.
  std::string weather_line_;

  /// The fences, and where the stock were behind them.
  ///
  /// The boundaries are fixed for a run and the grazed set is not, so they are
  /// kept apart: one is handed to the scene once, the other every frame. Under
  /// set stocking the day's list is every paddock on the farm, which is exactly
  /// what set stocking looks like and is worth being able to see.
  std::vector<core::Polygon> boundaries_;
  std::vector<std::vector<std::size_t>> grazed_each_day_;
  std::vector<std::vector<viz::MobMarker>> mobs_each_day_;

  /// What the run was carrying, for the line under the map. Taken once: a mob's
  /// class does not change through a year, and its head count does not either
  /// in this model - nothing is born, sold or dies.
  std::string stock_summary_;
  /// Indexed by Field.
  std::vector<std::pair<double, double>> whole_run_ranges_;
  std::vector<double> mean_cover_;

  /// The last run, kept so the report can be written from the same numbers the
  /// map is drawing rather than from a second run that might differ.
  std::optional<config::RunSummary> last_run_;
  std::optional<config::ScenarioBundle> last_bundle_;
  core::ManagementPolicy last_policy_;
  bool last_run_had_stock_ = false;
  std::string last_failure_;

  std::string data_directory_;

  int current_day_ = 0;
  Field field_ = Field::Cover;
  ScaleMode scale_mode_ = ScaleMode::WholeRun;
};

}  // namespace paddock::app
