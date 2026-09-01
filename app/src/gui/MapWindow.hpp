// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#pragma once

#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QListWidget>
#include <QMainWindow>
#include <QPoint>
#include <QPushButton>
#include <QSlider>
#include <QSplitter>
#include <QTimer>
#include <QVTKOpenGLNativeWidget.h>
#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <paddock/config/ScenarioComparison.hpp>
#include <paddock/config/ScenarioConfig.hpp>
#include <paddock/config/ScenarioRun.hpp>
#include <paddock/core/Farmer.hpp>
#include <paddock/core/PaddockMask.hpp>
#include <paddock/core/Raster.hpp>
#include <paddock/core/Terrain.hpp>
#include <paddock/viz/MapScene.hpp>
#include <paddock/viz/TerrainScene.hpp>

#include "PaddockInspector.hpp"
#include "SeasonChart.hpp"
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
  enum class Field : int {
    Cover = 0,
    SoilWater,
    AvailableWater,
    WaterStress,
    IrrigationToday,
    IrrigationToDate,
    Growth,
    LegumeFraction,
    Slope
  };

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
  void show_configuration(int ground, bool terrain, int heights = 1, bool irrigate = false);

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

  /// The whole window, controls and panel included.
  ///
  /// The render window screenshot shows the map and nothing around it, so the
  /// rows of controls under it - the ones that have twice been too wide for the
  /// window - cannot be checked from a screenshot at all. This can.
  bool save_window_screenshot(const std::string& path);

  /// Writes the current frame to a PNG.
  ///
  /// A map is a claim about what the model did, and a claim nobody looks at is
  /// not checked. This is how the map gets checked without a person at the
  /// screen: render a day, write it out, look at it. Returns false if the file
  /// could not be written.
  bool save_screenshot(const std::string& path);

  [[nodiscard]] std::size_t day_count() const noexcept { return dates_.size(); }

  /// Show a particular day, for the headless path.
  ///
  /// The width check below needs to step the run without a person dragging the
  /// timeline: the status lines change length as the days go by, and a label
  /// that sizes the window makes the map jump wider and narrower while it
  /// plays.
  void show_day_for_check(int day) { show_day(day); }

  /// Show a field by name, for the headless path. Returns false when no field
  /// goes by that name.
  bool select_field(const std::string& name);

  /// The range of a field over the run, for the headless path.
  ///
  /// Slope is the reason this exists: it is the one field a picture cannot
  /// show - on ground this flat the shading difference is under three per cent
  /// of brightness - so the only way to say how steep a farm is, is to print
  /// the number.
  [[nodiscard]] std::pair<double, double> field_range(Field field) const {
    return whole_run_range(field);
  }

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

  /// The disease report for the weather this run is on.
  ///
  /// **It reports the year on screen, not a decade.** How often a farm needs a
  /// zinc programme is a question about many years, and `paddock disease` is
  /// where you point at ten of them. What the window can honestly answer is
  /// what this year would have asked for, which is the year it is drawing.
  void open_disease_report();

  /// **Every indicator this run produced, with how far each can be trusted.**
  /// The report says what happened; this says where each figure stands against
  /// what it is measured by, and how many of them rest on evidence at all.
  void open_dashboard();

  /// **Downloads the ground this scenario named**, so that measured terrain is
  /// a button rather than an installation step.
  ///
  /// Snapshots are never shipped - they are bulk, they go stale, and the
  /// directory they live in is shared with sources whose licences forbid
  /// passing them on at all - so every download of this simulator arrives
  /// drawing flat. The instruction used to be to run a Python script, which
  /// asks somebody who wants to look at a farm to install a language runtime
  /// first.
  ///
  /// One known address, checked against a hash the bundle settled on before the
  /// request went out. It does not search: which of thousands of tiles covers a
  /// new farm is what scripts/nz-elevation-snapshot.py is for.
  void fetch_ground();
  void show_day(int day);
  void change_field(int field);
  void change_scale(int mode);
  void toggle_play();
  void advance_frame();

 private:
  /// Everything one run produces, gathered in one place.
  ///
  /// **It exists so a run can happen off the interface thread.** The day
  /// callback used to write straight into this window's members while the
  /// window was drawing them, which was safe only because the run blocked
  /// everything - and blocking everything is exactly the lag being fixed. A run
  /// now fills one of these on a worker and the interface adopts it whole when
  /// it is finished, so the two threads never touch the same vector.
  /// The ground a scenario named, where it is published, and where it goes.
  ///
  /// **Assembled on the worker thread and acted on from the window**, because
  /// the only thing that knows the ground is missing is the attempt to attach
  /// it, and the only thing that may open a dialog about it is the interface.
  struct GroundOffer {
    std::string url;
    std::string attribution;
    std::string destination;
    std::string sha256;
    /// Bundle name, for the sentence the dialog opens with.
    std::string scenario;
  };

  struct RunProducts {
    std::vector<core::Raster<double>> cover;
    std::vector<core::Raster<double>> soil_water;
    std::vector<core::Raster<double>> available_water;
    std::vector<core::Raster<double>> water_stress;
    std::vector<core::Raster<double>> irrigation_today;
    std::vector<core::Raster<double>> irrigation_to_date;
    std::vector<core::Raster<double>> growth;
    std::vector<core::Raster<double>> legume_fraction;
    std::vector<core::Raster<double>> slope;
    std::vector<std::string> dates;
    std::vector<double> mean_cover;
    std::string stock_summary;
    /// How full the root zone was when the irrigation schedule read it that
    /// morning, and why it held water back if it did. The decision was made on
    /// these and on nothing the rest of the run keeps.
    std::vector<core::Raster<double>> morning_water;
    std::vector<std::string> held_back_each_day;
    std::vector<std::vector<std::size_t>> grazed_each_day;
    /// Days since each paddock was last grazed, as the farm counted them that
    /// day. One entry per paddock, per day.
    std::vector<std::vector<int>> rest_days_each_day;
    std::vector<std::vector<viz::MobMarker>> mobs_each_day;
    std::vector<core::Polygon> boundaries;
    std::vector<core::Paddock> paddocks;
    std::vector<core::DailyWeather> weather;
    std::vector<double> irrigation_mm;
    core::IrrigationTally irrigation;
    std::optional<config::RunSummary> summary;
    std::optional<config::ScenarioBundle> bundle;
    std::optional<core::Raster<double>> elevation;
    core::ManagementPolicy policy;
    /// Whether this run was asked to irrigate, and on what rule. Kept so the
    /// inspector can say "off in this scenario" rather than reading a farm that
    /// simply never got dry enough as one that has no pivot.
    core::IrrigationPolicy irrigation_policy;
    double latitude_degrees = 0.0;
    bool had_stock = false;
    std::string no_ground_reason;
    /// Where the ground this scenario wanted is published, when it is missing
    /// and can be fetched. Empty when the ground is here, when the scenario is
    /// flat, or when the bundle does not say where its ground came from.
    std::optional<GroundOffer> ground_offer;
    /// What went wrong, when something did. The run is discarded and the
    /// interface says this instead.
    std::string failure;
    /// Whether the bundle brought its own survey, which the panel says out
    /// loud because it means the ground list above it does not apply.
    bool measured_ground = false;
  };

  /// Runs one scenario. **Static, and touching nothing of the window**, because
  /// it runs on a worker thread.
  [[nodiscard]] static RunProducts simulate(const SetupPanel::Choices& choices);

  /// Takes a finished run and puts it on screen. Runs on the interface thread.
  void adopt_run(RunProducts products);

  /// Whether a run is in flight. A second one started over the top of the first
  /// would have two workers writing one window.
  bool running_ = false;

  /// A run was asked for while one was already going.
  ///
  /// **Remembered rather than dropped.** Turning irrigation on fires the
  /// panel's own re-run and then the caller asks for one too; a guard that
  /// simply refused the second left the farm showing the settings from before
  /// the change, silently. One is remembered and started when the first
  /// finishes - more than one would be the same run queued twice.
  bool rerun_wanted_ = false;

  /// Steps the bundle with its stock on it, under `policy`, keeping every day's
  /// rasters as it goes.
  static void simulate_managed(RunProducts& into, const config::ScenarioBundle& bundle,
                               const core::ManagementPolicy& policy,
                               const core::IrrigationPolicy& irrigation,
                               const core::IrrigationSystem& system);

  /// Steps the pasture alone, for a bundle that carries no stock. Both paths
  /// exist because `canterbury-baseline` has no mobs and is still worth looking
  /// at; only this one can be taken for such a bundle, and a managed run of it
  /// would have no mob to report on.
  static void simulate_pasture_only(RunProducts& into, const config::ScenarioBundle& bundle,
                                    const core::IrrigationPolicy& irrigation,
                                    const core::IrrigationSystem& system);

  /// Empties the per-day series, so a second run does not append to the first.
  void clear_series();

  /// Whole-run colour ranges and the timeline, once a run has been captured.
  void adopt_series();

  static void keep_day(RunProducts& into, const core::FarmletGrid& grid, const std::string& date);

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

  /// Inspects the paddock under a point in the render window, given in VTK
  /// display pixels from the bottom left, and returns what the readout says.
  ///
  /// Here so that the inspector can be exercised by something other than a
  /// person clicking: the chain it runs - a screen point, the ground under it,
  /// the paddock that contains it, the cells that paddock owns - has four
  /// places to get a coordinate frame wrong, and every one of them produces a
  /// plausible answer for the wrong piece of ground.
  /// Moves the timeline to `day`, as dragging it would. Out-of-range days are
  /// clamped rather than refused, so a caller asking for "the last day" need
  /// not know how long the run was.
  void go_to_day(int day);

  /// Slides the terrain view, as the slider beside it would. `percent` runs
  /// from -100 (a screenful down, towards the paddocks) to 100 (up, towards
  /// the sky).
  void slide_view(int percent);

  /// Turns every layer of the scene on, as ticking each box would. Here so the
  /// stack can be drawn by something other than a person clicking four times.
  void show_all_layers();

  /// Adds the panel as it stands as a named scenario, as the button would but
  /// without asking for the name.
  ///
  /// Here so the comparison can be exercised by something other than a person
  /// clicking: a run of five scenarios, a table and a paragraph is the largest
  /// thing in this window and none of it is in a screenshot of the map.
  void keep_scenario(const QString& name);

  /// Turns irrigation on or off in the panel, as the tick box would.
  void select_irrigation(bool on);

  /// Runs every stored scenario and returns the table as Markdown. Empty when
  /// there are fewer than two, or when a run failed.
  /// `failure` names what went wrong when the result is empty. An empty string
  /// with no reason is the kind of silence that costs an afternoon.
  [[nodiscard]] std::string comparison_markdown(QString& failure);

  /// Writes the report on the run currently drawn to a PDF.
  ///
  /// Here so a page can be looked at without a person clicking through two
  /// dialogues - and a PDF is the one thing in this window whose faults are
  /// invisible until somebody opens the file.
  ///
  /// False with `failure` set when there is no finished run to report on, or
  /// when the file could not be written. The reason is passed back rather than
  /// printed here, because the only caller is the command line and it owns what
  /// goes to the terminal.
  [[nodiscard]] bool save_run_pdf(const std::string& path, std::string& failure);

  /// Whether the pivots are turning.
  ///
  /// Exposed because this is the one thing about the irrigation picture that a
  /// screenshot cannot show, and it is the thing that broke: ticking the layer
  /// told the scene the spray was wanted but never handed it a day, so the
  /// arms stood still on exactly the day somebody had asked to watch.
  /// Waits for a run in flight to finish, pumping the event loop while it
  /// does.
  ///
  /// **For the command line, which is a batch mode.** A run is on a worker now
  /// so the window keeps drawing, and every check in main() assumed it was over
  /// the moment start_run returned - the first thing that happened was a smoke
  /// test reporting a farm of zero days. A person clicking never needs this;
  /// a script always does.
  void wait_for_run() const;

  [[nodiscard]] bool irrigation_animating() const;

  /// Water put on across the farm on the day being shown, mm.
  [[nodiscard]] double irrigation_today_mm() const;

  /// Switches between the flat map and the terrain view, as the drop-down
  /// would.
  void select_view(bool terrain);

  /// Which of the two is showing.
  [[nodiscard]] bool showing_terrain() const noexcept { return showing_terrain_; }

  [[nodiscard]] std::string inspect_pixel(int x, int y);

  /// The render window's size in device pixels, which is the frame
  /// inspect_pixel counts in.
  /// The ground under a point in the render window, or nothing when the point
  /// missed the farm. Exposed so a caller can check the coordinate frame
  /// itself rather than trusting that a plausible paddock name means the right
  /// piece of ground was found.
  [[nodiscard]] std::optional<core::Point2D> ground_under(int x, int y) const;

  [[nodiscard]] int render_width() const;
  [[nodiscard]] int render_height() const;

  /// What the run watered over the year. Reported by the headless path because
  /// a screenshot cannot show a season's water.
  [[nodiscard]] const core::IrrigationTally& irrigation() const noexcept {
    return irrigation_tally_;
  }

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

  /// Whether the weather is drawn over the farm.
  ///
  /// Off makes the map exact: a cloud is translucent by construction, and
  /// anything translucent between the camera and the paddocks shifts a colour
  /// the reader is meant to match against the legend. On shows what the day
  /// was like. Neither serves both jobs, so it is a control rather than a
  /// decision made for somebody.

  /// How far above the horizon the three-dimensional view looks from.
  ///
  /// Beside the scene rather than under it, because it belongs to the picture
  /// and not to the run. Dragging with the mouse can put the camera anywhere,
  /// including under the ground; this is the one axis worth adjusting on
  /// purpose - how much of the farm is in view against how much of its shape -
  /// and it always lands somewhere the farm is visible from.
  QSlider* pan_slider_ = nullptr;

  /// One box per layer of the scene, in the order the layers are stacked:
  /// weather above, the pasture on the ground, and then a sheet for each field
  /// of kStackedFields. Held so the row can be enabled and disabled with the
  /// terrain view.
  std::vector<QCheckBox*> layer_boxes_;

  /// One scenario somebody is comparing: what they called it, how the panel was
  /// set when they added it, and what it came to when it was last run.
  ///
  /// The settings are kept rather than the run, because a run is the answer and
  /// the settings are the question. Re-running is cheap - a year over this farm
  /// is under half a second - and a stored answer would go stale the moment the
  /// weather or the bundle changed underneath it.
  struct StoredScenario {
    QString name;
    SetupPanel::Choices choices;
    std::optional<config::RunSummary> result;
    double hectares = 0.0;
    /// How the panel was set, as label and value, for the comparison's header.
    std::vector<std::pair<std::string, std::string>> settings;
  };

  std::vector<StoredScenario> scenarios_;

  /// The run against time, beside the map. The map answers where, this answers
  /// when, and a farm adviser's two questions are which paddock and which
  /// month.
  SeasonChart* chart_ = nullptr;

  /// The map over the readings. Kept so the map's share of the height can be
  /// set once the window knows how tall it is.
  QSplitter* scene_split_ = nullptr;

  /// The coloured names of whatever the chart is drawing, above it.
  QLabel* chart_key_ = nullptr;

  /// Which quantities the chart is drawing. One box each, ticked by the person
  /// looking.
  ///
  /// **Two axes carry all of them, so there is no need for more.** Everything
  /// the model produces daily is either in the farm's own working units - kg
  /// DM/ha, mm - or a share between nought and one. Left axis for the first,
  /// right for the second. Four axes would mean two a side and a reader
  /// matching lines to axes by colour, which is the thing charts do worst.
  std::vector<QCheckBox*> chart_boxes_;

  /// Which quantities are plotted, oldest choice first.
  ///
  /// **Two at a time, and a third displaces the first.** Kept in the order they
  /// were chosen, because that decides which axis each gets and which one goes
  /// when a third is asked for. Refusing the third instead would leave somebody
  /// clicking a box that does nothing and having to work out for themselves
  /// which of the others to turn off.
  std::vector<std::size_t> chart_order_;

  /// Whether the days that were irrigated are marked under the plot.
  ///
  /// **A box of its own, because it appeared without being asked for.** Two
  /// quantities were ticked and three things showed up in the key; the third
  /// was this row of marks, which needs no axis and so was never part of the
  /// two. Everything drawn is now something somebody chose.
  QCheckBox* irrigated_days_box_ = nullptr;

  /// Adds or removes a quantity from the plot, displacing the oldest when a
  /// third is asked for.
  void choose_series(std::size_t which, bool wanted);

  /// Fills the chart from the run that has just finished.
  void refresh_chart();

  QListWidget* scenario_list_ = nullptr;
  QPushButton* add_scenario_button_ = nullptr;
  QPushButton* compare_button_ = nullptr;

  /// Opens the report on the run currently drawn, as opposed to the comparison
  /// of several. Under the readings it expands on.
  QPushButton* run_report_button_ = nullptr;
  QPushButton* disease_report_button_ = nullptr;
  QPushButton* dashboard_button_ = nullptr;
  QPushButton* fetch_ground_button_ = nullptr;

  /// The last table produced, so the report can be reopened without running
  /// everything again. Empty until something has been run.
  std::optional<config::ComparisonTable> last_report_;

  /// Opens the report on the last run.
  void open_comparison_report();

  /// Snapshots the panel as a scenario. Refuses past the limit rather than
  /// silently dropping one.
  void add_scenario();

  /// Runs every stored scenario and shows the table.
  void run_comparison();

  /// Runs every stored scenario. Names the first failure in `failure` and stops
  /// there: a table with one scenario missing is worse than none, because the
  /// gap is not visible in it.
  ///
  /// `flat_ground` collects the reason any scenario had to be drawn flat. A
  /// farm that ran without its survey is comparable with one that did, but
  /// nobody should have to guess which happened.
  [[nodiscard]] std::vector<config::ComparedScenario> run_scenarios(
      QString& failure, std::vector<std::string>& flat_ground);

  /// Loads the chosen scenario back into the panel and runs it, so the map
  /// beside the list is showing the scenario the list has selected.
  void show_scenario(int index);

  /// Redraws the list from scenarios_.
  void refresh_scenario_list();

  /// What is happening, while it is happening, and what happened when it is
  /// done.
  ///
  /// **A window that stops answering is indistinguishable from a broken one.**
  /// The run moved to a worker so the interface keeps drawing; these say what
  /// it is drawing about. The banner sits over the map while work is in flight;
  /// the note appears in the top right when it finishes and fades.
  QLabel* progress_label_ = nullptr;
  QLabel* notice_label_ = nullptr;
  QTimer* notice_timer_ = nullptr;

  void show_progress(const QString& what);
  void hide_progress();

  /// Says what happened, in the corner, for a few seconds. `good` picks the
  /// colour: a failure that looked like a success would be worse than silence.
  void announce(const QString& what, bool good);

  /// Keeps the two floating labels in their corners when the window resizes.
  void place_notices();

  /// Fills the terrain view's stack with today's values for kStackedFields.
  void refresh_stack(std::size_t day);

  /// Hands the terrain view the day's irrigation: where it landed, how much
  /// each paddock got, and what a full bar means.
  void refresh_irrigation(std::size_t day);

  /// **Runs while the timeline is paused, and that is deliberate.**
  ///
  /// The spray's travelling wave says "this paddock is being watered today" -
  /// a state, not a rate - so it keeps going on a day somebody is sitting on.
  /// It carries no quantity; the bars round the paddocks do, and they are
  /// still. Stopped whenever there is nothing to animate, so a farm that is not
  /// being watered is not repainting itself sixty times a second for nothing.
  QTimer* spray_timer_ = nullptr;
  double spray_phase_ = 0.0;
  QComboBox* field_box_ = nullptr;
  QComboBox* scale_box_ = nullptr;
  QSlider* timeline_ = nullptr;
  QPushButton* play_button_ = nullptr;
  QLabel* date_label_ = nullptr;
  QLabel* summary_label_ = nullptr;

  /// The day's weather, above the map rather than below it: it is the thing
  /// driving what the map shows, so it reads first.
  QLabel* weather_label_ = nullptr;

  /// What the paddock under the last click is doing, on the day being shown.
  PaddockInspector* inspector_ = nullptr;

  /// The selected paddock's day, gathered from the run's own records. Empty
  /// when nothing is selected or the run kept no mask to select from.
  [[nodiscard]] std::optional<config::PaddockInspection> inspect_selected() const;

  /// What the last run came to, sent over by the panel. Beside the map it
  /// describes rather than in the corner the run was set up in.
  QLabel* results_label_ = nullptr;
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
  std::optional<GroundOffer> ground_offer_;

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

  /// The slope of each cell, in degrees. One frame, not one per day: the
  /// ground does not move.
  ///
  /// It is here because it is invisible in the picture and it drives two
  /// sourced pieces of the model. On a farm falling six metres in a kilometre
  /// the shading difference between the steepest and the flattest cell is
  /// under three per cent of brightness, so no amount of lighting will show
  /// it - but that same slope sets the energy cost of walking (TMC Eq. 23) and
  /// the radiation a face receives (Gillingham et al.). A quantity that
  /// changes the answer and cannot be seen is worth a colour ramp of its own.
  std::vector<core::Raster<double>> slope_;

  /// The share of each cell's water still there, 0 to 1. The same fact as the
  /// soil water depth read the way a farmer reads it, and the map that makes
  /// an irrigation trigger legible: the rule fires at a percentage, so a map
  /// in millimetres cannot show why it fired.
  std::vector<core::Raster<double>> available_water_;

  /// Where the water went, on the day and over the run.
  ///
  /// A mean over the farm cannot show these: the whole point of a rule that
  /// waters cell by cell is that some ground gets water and some does not.
  std::vector<core::Raster<double>> irrigation_today_;
  std::vector<core::Raster<double>> irrigation_to_date_;

  /// What the pasture grew each day, kg DM/ha. The production side on its own:
  /// cover moves for two reasons at once and cannot answer which part of the
  /// farm is paying its way.
  std::vector<core::Raster<double>> growth_;

  /// The paddocks this run was over, kept whole rather than as bare rings: the
  /// inspector reports the name a farmer calls the paddock by, and a polygon
  /// does not carry one.
  std::vector<core::Paddock> paddocks_;

  /// Which paddock owns each cell. Built once a run has finished, because the
  /// fences do not move during it, and **it is the same partition the model
  /// uses** - an inspector that decided ownership its own way could report a
  /// paddock average over a different set of cells than the one the simulation
  /// grazed.
  std::optional<core::PaddockMask> mask_;

  /// The paddock the last click landed in, if any. Nothing means the click
  /// missed the farm, or nobody has clicked yet.
  std::optional<std::size_t> selected_paddock_;

  /// Where the left button went down, so a drag that turns the scene is not
  /// mistaken for a click that asks about a paddock.
  QPoint pressed_at_;

  /// Resolves a click to a paddock and writes the readout.
  void inspect_at(int x, int y);

  /// Writes the readout for `selected_paddock_` on the day being shown. Called
  /// again whenever the day or the run changes, so the numbers on screen are
  /// never from a day other than the map beside them.
  void show_selected_paddock();

  /// The mean of `series` over the cells the mask gives `paddock`, on `day`.
  [[nodiscard]] std::optional<double> paddock_mean(const std::vector<core::Raster<double>>& series,
                                                   std::size_t paddock, std::size_t day) const;

 protected:
  bool eventFilter(QObject* watched, QEvent* event) override;

  /// Escape clears the paddock selection.
  ///
  /// A selection has to be droppable, and clicking off the farm is a poor way
  /// to do it: the ground fills most of the view, so "off the farm" is a small
  /// target and hitting it by accident is easier than hitting it on purpose.
  void keyPressEvent(QKeyEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;

 private:
  /// What the run watered, day by day, as a mean over the farm in mm, and what
  /// the year came to. Empty when nothing was irrigated.
  std::vector<double> irrigation_mm_;
  core::IrrigationTally irrigation_tally_;
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

  /// The soil as the irrigation schedule read it each morning, and its reason
  /// for holding water back that day.
  std::vector<core::Raster<double>> morning_water_;
  std::vector<std::string> held_back_each_day_;

  /// The rest the farm had given each paddock, day by day. Kept for the
  /// inspector, which reports it rather than working it out.
  std::vector<std::vector<int>> rest_days_each_day_;
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
  core::IrrigationPolicy last_irrigation_policy_;

  bool last_run_had_stock_ = false;
  std::string last_failure_;

  std::string data_directory_;

  int current_day_ = 0;
  Field field_ = Field::Cover;
  ScaleMode scale_mode_ = ScaleMode::WholeRun;
};

}  // namespace paddock::app
