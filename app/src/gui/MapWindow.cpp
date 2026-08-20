// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#include "MapWindow.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QDockWidget>
#include <QEvent>
#include <QEventLoop>
#include <QFrame>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSplitter>
#include <QVBoxLayout>
#include <QWidget>
#include <QtConcurrent>
#include <algorithm>
#include <array>
#include <cstddef>
#include <exception>
#include <functional>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vtkCamera.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkNew.h>
#include <vtkPNGWriter.h>
#include <vtkRenderWindow.h>
#include <vtkRenderer.h>
#include <vtkWindowToImageFilter.h>

#include <paddock/config/ScenarioComparison.hpp>
#include <paddock/config/ScenarioReport.hpp>
#include <paddock/core/FarmletGrid.hpp>
#include <paddock/core/Solar.hpp>
#include <paddock/core/Weather.hpp>

#include "../AttachElevation.hpp"
#include "ComparisonDialog.hpp"
#include "ReportDialog.hpp"

namespace paddock::app {

namespace {
/// How wide the setup dock opens, in pixels. Comfortably past the panel's own
/// minimum, so the form is not on the edge of scrolling the moment it opens.
constexpr int kOpeningPanelWidth = 520;

/// The most scenarios a comparison holds.
///
/// Five, because a table wider than that stops being read across: the eye has
/// to carry a row from one column to another, and past five columns it starts
/// carrying the wrong one. It is a limit on the table, not on the model.
constexpr int kMostScenarios = 5;

/// How the side of the window is divided between the panel and the list, as
/// layout stretch. Four to one, so the list is about a fifth of the height.
constexpr int kPanelShare = 4;
constexpr int kScenarioListShare = 1;

/// How the window divides, as a share of the height it has.
///
/// **The map takes most of it, because the map is the thing.** Everything under
/// it is read once and then glanced at; the farm is looked at continuously, and
/// at a quarter of the window there was not enough of it to see. The handle
/// still moves - these are opening proportions, not limits.
constexpr double kMapShareOfHeight = 0.65;

/// The least the map is ever given, in pixels, however the window is resized.
/// Below this a farm a kilometre across is a smudge.
constexpr int kSmallestMapHeight = 320;

/// Roughly what the rows of controls under the splitter take, in pixels, so the
/// share above is of the space the splitter actually has.
constexpr int kControlsAllowance = 120;

/// How many quantities the chart draws at once.
///
/// **Two, so each can own an axis.** An axis carrying one quantity can be
/// titled with its name, its unit and its own range; one carrying three has to
/// be titled by unit and scaled to whichever runs highest, and the reader is
/// back to matching lines to axes by colour.
constexpr std::size_t kMostPlotted = 2;

/// The fewest scenarios a comparison needs. One is a simulation, and the panel
/// runs that.
constexpr std::size_t kFewestCompared = 2;

/// What the chart can draw, and which side of it each reads off.
///
/// **Two axes carry all of these.** Everything the model produces daily is
/// either in the farm's own working units - kilograms of dry matter a hectare,
/// millimetres of water - or a share between nought and one. The first group
/// reads off the left, the second off the right, and that is the whole of it.
/// A third and fourth axis would mean two a side, with the reader matching
/// lines to axes by colour, which is the thing a chart does worst.
struct ChartSeries {
  const char* name;
  /// The short name the tick box carries. The row of them has to fit across a
  /// pane that shares its width with the readings, and the key underneath
  /// gives the full name against its colour - so the box only has to be
  /// recognisable, not complete.
  const char* label;
  const char* unit;
  MapWindow::Field field;
  int red;
  int green;
  int blue;
  bool on;
};

constexpr std::array<ChartSeries, 6> kChartSeries{{
    {"Pasture cover", "Cover", "kg DM/ha", MapWindow::Field::Cover, 94, 168, 84, true},
    {"Soil moisture", "Moisture", "of capacity", MapWindow::Field::AvailableWater, 68, 130, 175,
     true},
    {"Growth", "Growth", "kg DM/ha", MapWindow::Field::Growth, 168, 200, 90, false},
    // **Not called "Irrigation", because the row of marks below already is.**
    // Both would have appeared in the key at once, under one name, one a line
    // and one a set of dots.
    {"Water applied", "Water on", "mm", MapWindow::Field::IrrigationToday, 120, 190, 245, false},
    {"Water stress", "Stress", "of capacity", MapWindow::Field::WaterStress, 214, 132, 74, false},
    {"Legume", "Clover", "of capacity", MapWindow::Field::LegumeFraction, 176, 140, 220, false},
}};
constexpr int kOpeningReadingsWidth = 620;
constexpr int kOpeningChartWidth = 700;

/// How far the floating notices sit in from the corner of the map, and how long
/// the one in the corner stays. Long enough to be read on the way past, short
/// enough not to sit over the farm while somebody works.
constexpr int kNoticeMargin = 12;
constexpr int kNoticeSeconds = 4;

/// How long each turn of the event loop is given while waiting for a run, in
/// milliseconds. Short enough that the wait ends promptly, long enough not to
/// spin the processor for nothing.
constexpr int kWaitSliceMs = 20;

/// How often the spray's wave is redrawn, and how far it moves each time.
/// Twenty-five frames a second, a full sweep in four seconds: fast enough to
/// read as water moving, slow enough that it is not a strobe over the paddocks.
constexpr int kSprayFrameInterval = 40;
constexpr double kSprayPhaseStep = 0.01;

/// The fields drawn as sheets under the pasture, top to bottom.
///
/// **Ordered as the causal chain runs, the same as the map-mode list.** What
/// grew sits directly under the pasture it made; under that the moisture that
/// allowed it; under that the water put on and the water put on so far; under
/// that the stress the shortfall caused; and the legume share last, which is
/// the one that moves over seasons rather than days.
///
/// Slope is not here: it has one frame for the whole run, and a sheet that
/// never changes while the others do would read as a fault. Soil water is not
/// here either - it is the same fact as soil moisture in millimetres instead of
/// a share, and two sheets of one quantity is a stack padded out.
constexpr std::array<MapWindow::Field, 6> kStackedFields{
    MapWindow::Field::Growth,          MapWindow::Field::AvailableWater,
    MapWindow::Field::IrrigationToday, MapWindow::Field::IrrigationToDate,
    MapWindow::Field::WaterStress,     MapWindow::Field::LegumeFraction};

/// How far the pointer may move between press and release and still count as a
/// click rather than a drag. A hand moves a little on a mouse button.
constexpr int kClickSlopPixels = 4;
}  // namespace

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

/// The same, for the stock themselves.
const std::vector<viz::MobMarker> kNobodyOnTheFarm;

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
      return {"Soil water", "Soil water (mm)", viz::Ramp::SoilWater, false, 0.0, 0.0};
    case MapWindow::Field::WaterStress:
      return {"Water stress", "Water stress (1 = unstressed)", viz::Ramp::Viridis, true, 0.0, 1.0};
    case MapWindow::Field::LegumeFraction:
      return {"Legume fraction", "Legume share of green DM", viz::Ramp::Viridis, true, 0.0, 1.0};
    case MapWindow::Field::AvailableWater:
      return {"Soil moisture",
              "Water left of what the soil can hold",
              viz::Ramp::SoilWater,
              true,
              0.0,
              1.0};
    case MapWindow::Field::IrrigationToday:
      return {"Irrigation today", "Water put on today (mm)", viz::Ramp::SoilWater, false, 0.0, 0.0};
    case MapWindow::Field::IrrigationToDate:
      return {
          "Irrigation to date", "Water put on so far (mm)", viz::Ramp::SoilWater, false, 0.0, 0.0};
    case MapWindow::Field::Growth:
      return {"Growth today",
              "Pasture grown today (kg DM/ha)",
              viz::Ramp::PastureGreen,
              false,
              0.0,
              0.0};
    case MapWindow::Field::Slope:
      return {"Slope", "Slope (degrees)", viz::Ramp::Viridis, false, 0.0, 0.0};
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
  // Ordered as the causal chain runs, so the list itself reads as the story:
  // the ground dries, the rule fires, the water lands, the stress lifts.
  for (const MapWindow::Field field :
       {Field::Cover, Field::Growth, Field::AvailableWater, Field::IrrigationToday,
        Field::IrrigationToDate, Field::WaterStress, Field::SoilWater, Field::LegumeFraction,
        Field::Slope}) {
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

  weather_label_ = new QLabel(this);
  weather_label_->setTextFormat(Qt::RichText);

  paddock_label_ = new QLabel(this);
  paddock_label_->setTextFormat(Qt::RichText);

  results_label_ = new QLabel(this);
  results_label_->setTextFormat(Qt::RichText);
  results_label_->setWordWrap(true);
  paddock_label_->setText("<b>Paddock</b> &nbsp; click the map to inspect one");

  // **These two lines must not set the width of the window.**
  //
  // Both change length as the run plays: "dry" becomes "12.3 mm", a
  // three-digit radiation becomes two, the ground note appears and
  // disappears. A label's size hint is its text, so with the default policy
  // every one of those resized the window - the map jumped a few pixels wider
  // and narrower as the days went by, which reads as a rendering fault and
  // makes the picture impossible to compare from one day to the next.
  //
  // Ignored means the hint is not consulted for the layout at all: the line
  // takes whatever width the controls row settles on, and the window stays
  // where the person using it put it. Word wrapping would fix the width and
  // move the problem to the height, which is worse - the map would grow and
  // shrink instead.
  // **They wrap now, and no longer set the width of anything.**
  //
  // On one line these were truncated mid-sentence; in a column of their own
  // they have somewhere to go. Ignored horizontally still, because a label's
  // size hint is its text and a hint that reaches the layout drags the window
  // about as the run plays - the fault that made the map jump a few pixels
  // wider and narrower day by day.
  for (QLabel* line : {weather_label_, summary_label_, paddock_label_, results_label_}) {
    line->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Minimum);
    line->setWordWrap(true);
    line->setAlignment(Qt::AlignTop | Qt::AlignLeft);
  }

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
  // The shipped farms are on the Canterbury Plains and their steepest cell is
  // under one and a half degrees. Lit at a true scale, the brightest and
  // darkest ground on them differ by about 1.6% - invisible, and no choice of
  // sun angle changes that, because the ground and not the light is what is
  // flat. These two are what make relief visible on them at all, and the
  // factor stays in the label because a farm drawn twenty times too steep must
  // never be mistaken for one that is.
  height_box_->addItem("Heights x10", 10);
  height_box_->addItem("Heights x20", 20);
  height_box_->setEnabled(false);

  // **Two rows, because one did not fit.**
  //
  // Five drop-downs, a checkbox, a button, a timeline and a date on a single
  // line came to more than the window was wide, and the timeline - the control
  // people actually drag - was the one squeezed to nothing, because it is the
  // only one that gives up space. Splitting them puts the whole width under the
  // timeline and keeps the day's date where it can be read.
  //
  // The playing controls go on top, nearest the map they move: play, the
  // timeline, the date. What is being drawn goes underneath, because those are
  // set once and then left alone.
  auto* playing = new QHBoxLayout;
  playing->addWidget(play_button_);
  playing->addWidget(timeline_, 1);
  playing->addWidget(date_label_);

  auto* choices = new QHBoxLayout;
  choices->addWidget(view_box_);
  choices->addWidget(height_box_);
  choices->addWidget(field_box_);
  choices->addWidget(scale_box_);
  choices->addStretch(1);

  // **The layers, in the order they are stacked, from the sky down.**
  //
  // Read left to right this row is a section through the farm: weather above
  // it, the sward on the surface, the root zone the water balance is about,
  // and the ground below that. The order is the profile's, not a list of
  // features, so somebody who has never seen the window can tell which is on
  // top of which without turning any of them on.
  //
  // The weather box moves in here from the row above, where it sat among the
  // drop-downs as though it were a different kind of thing. It is the top
  // layer, and it belongs with the rest of them.
  auto* layers = new QHBoxLayout;
  layers->addWidget(new QLabel("Layers", this));

  // **The stack, top to bottom, and the row reads as the stack does.**
  //
  // Weather above the farm, the pasture on the ground, and then one sheet per
  // field: what grew, the moisture that allowed it, the water put on, the water
  // put on so far, the stress the shortfall caused, and the clover. Left to
  // right is top to bottom, so somebody who has never seen the window can tell
  // which sheet is which without turning any of them on.
  //
  // Only the first two start ticked. A farm opens as a farm; the stack is
  // something to turn on when a question needs it.
  const auto add_box = [this, layers](const QString& name, const QString& explanation, bool on,
                                      const std::function<void(bool)>& toggled) {
    auto* check = new QCheckBox(name, this);
    check->setChecked(on);
    check->setToolTip(explanation);
    connect(check, &QCheckBox::toggled, this, [this, toggled](bool ticked) {
      toggled(ticked);
      // **Turning a layer on reframes the scene; turning one off does not.**
      //
      // The camera is placed from the bounds of what can be seen and a hidden
      // sheet is not in them, so a stack revealed after the view was framed
      // hangs off the bottom of the window. Ticking a box and seeing nothing is
      // the box not working. Hiding does not reframe, or somebody who had
      // zoomed in on a corner would lose it for switching the sky off.
      if (ticked) {
        terrain_.reset_camera();
        pan_slider_->setValue(0);
      }
      if (showing_terrain_) {
        view_->renderWindow()->Render();
      }
    });
    layer_boxes_.push_back(check);
    layers->addWidget(check);
  };

  add_box("Weather",
          "The day's sun, cloud, rain and wind, drawn above the farm.\n\nTurn it off to read "
          "the map exactly: cloud is translucent, and anything translucent over the paddocks "
          "shifts the colour you are matching against the legend.",
          true, [this](bool on) { terrain_.show_layer(viz::TerrainScene::Layer::Weather, on); });
  add_box("Pasture",
          "The ground surface, coloured by whichever field the map mode names, with the fences "
          "and the stock that stand on it.",
          true, [this](bool on) { terrain_.show_layer(viz::TerrainScene::Layer::Pasture, on); });

  for (std::size_t i = 0; i < kStackedFields.size(); ++i) {
    const FieldStyle style = style_of(kStackedFields[i]);
    add_box(QString(style.label),
            QString("%1, drawn as a sheet under the pasture, on the same ground and with the "
                    "same fences - dimmer, because down there they are for finding a paddock "
                    "rather than for reading.")
                .arg(style.legend),
            false, [this, i](bool on) {
              terrain_.show_stack_layer(i, on);
              // Today's irrigation brings its spray with it. The sheet says
              // where the water went; the spray puts it over the paddocks it
              // went onto, which is the same fact seen from the farm rather
              // than from a legend.
              if (kStackedFields.at(i) == Field::IrrigationToday) {
                terrain_.show_spray(on);
                // **And the day has to be handed over again, or nothing turns.**
                //
                // Starting and stopping the animation is refresh_irrigation's
                // job, because that is where the day's water is known. Ticking
                // the box only told the scene it was wanted; the timer stayed
                // stopped until something else happened to redraw a day, so the
                // arms stood still on the very day somebody had just asked to
                // watch.
                refresh_irrigation(static_cast<std::size_t>(std::max(0, current_day_)));
              }
            });
  }
  layers->addStretch(1);

  // **Slides the view up and down, and that is what it is for.**
  //
  // The camera is framed around everything drawn, and the sun and cloud sit
  // well above the farm - so zooming in on a farm that sits at the bottom of
  // that volume walks the pasture off the bottom of the window. Dragging it
  // back with the mouse also turns the scene, which loses the bearing the
  // compass was added to keep. This moves what is on screen and nothing else.
  //
  // Zero in the middle, so the control shows at a glance whether the view has
  // been moved, and returns to the framing exactly by coming back to the centre.
  pan_slider_ = new QSlider(Qt::Vertical, this);
  pan_slider_->setRange(-100, 100);
  pan_slider_->setValue(0);
  pan_slider_->setTickPosition(QSlider::TicksBothSides);
  pan_slider_->setTickInterval(50);
  pan_slider_->setToolTip(
      "Slide the view up and down.\n\n"
      "Up looks towards the sky, down towards the paddocks. Zoomed in, the farm can sit "
      "below the bottom of the window because the sun and cloud are drawn well above it; "
      "this brings it back without turning the scene. The middle is where the view was "
      "framed.");
  connect(pan_slider_, &QSlider::valueChanged, this, [this](int position) {
    if (!showing_terrain_) {
      return;
    }
    // A whole screenful at each end, which is enough to bring a farm back from
    // either edge without letting the slider throw it clean out of sight.
    terrain_.pan_vertically(position / 100.0);
    view_->renderWindow()->Render();
  });

  // **The map and the year, side by side.**
  //
  // The scene shows one day over the whole farm; the chart shows the whole run
  // at one place on it. Neither answers the other's question, and a window that
  // offered only the first made somebody scrub the timeline to find out when
  // anything happened.
  //
  // The map takes three quarters: it is the thing being driven, and the chart
  // is read rather than manipulated. A splitter, so anybody who disagrees can
  // move it.
  chart_ = new SeasonChart(this);
  connect(chart_, &SeasonChart::dayPicked, this, &MapWindow::go_to_day);

  auto* scene_side = new QHBoxLayout;
  scene_side->setContentsMargins(0, 0, 0, 0);
  scene_side->addWidget(view_, 1);
  scene_side->addWidget(pan_slider_);
  auto* scene_holder = new QWidget(this);
  scene_holder->setLayout(scene_side);

  // **The map above, and under it what was read off it.**
  //
  // The lower half divides again: the day's readings on the left, the whole run
  // on the right. They are the two ways of asking the same question - what is
  // happening, and when did it happen - and the map above them is the third,
  // which is where.
  //
  // The readings move down here because on one line they were being truncated:
  // a weather line, a paddock and a farm summary each cut off mid-sentence at
  // the width of the window. Given a column they wrap.
  // **The readings scroll.** Four lines that each wrap to three is more than
  // any fixed strip holds, and the strip is now smaller because the map takes
  // most of the height. Clipped text is worse than a scroll bar: a reader who
  // cannot see there is more will not look for it.
  auto* readings = new QVBoxLayout;
  readings->setContentsMargins(10, 8, 10, 8);
  readings->setSpacing(7);
  readings->addWidget(weather_label_);
  readings->addWidget(paddock_label_);
  readings->addWidget(summary_label_);
  readings->addWidget(results_label_);

  // **The report on this run, under the readings it summarises.** It was a
  // button in the setup panel, which is where a run is arranged rather than
  // where it is read; here it sits at the foot of the numbers it expands on.
  run_report_button_ = new QPushButton("Export this run's report", this);
  run_report_button_->setToolTip(
      "The full report on the run on screen: what the farmer did, what the stock did, and "
      "whether the budgets balanced.\n\nSaved as a PDF or as Markdown. The comparison of several "
      "scenarios is the other report, beside the scenario list.");
  run_report_button_->setEnabled(false);
  connect(run_report_button_, &QPushButton::clicked, this, &MapWindow::open_report);

  readings->addStretch(1);

  auto* readings_inner = new QWidget(this);
  readings_inner->setLayout(readings);

  auto* readings_scroll = new QScrollArea(this);
  readings_scroll->setWidget(readings_inner);
  readings_scroll->setWidgetResizable(true);
  readings_scroll->setFrameShape(QFrame::NoFrame);
  readings_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

  // **The button sits outside the scrolling part, pinned at the foot.**
  // Inside it, the readings pushed it below the fold and it could only be
  // reached by scrolling to look for it - and a button nobody can see is worse
  // than a line of text nobody can see, because there is nothing to hint that
  // it is there.
  auto* report_row = new QHBoxLayout;
  report_row->setContentsMargins(10, 0, 10, 8);
  report_row->addWidget(run_report_button_);
  report_row->addStretch(1);

  auto* readings_column = new QVBoxLayout;
  readings_column->setContentsMargins(0, 0, 0, 0);
  readings_column->setSpacing(0);
  readings_column->addWidget(readings_scroll, 1);
  readings_column->addLayout(report_row);

  auto* readings_holder = new QWidget(this);
  readings_holder->setLayout(readings_column);

  // The chart with its colour key over it. Which colour is which is the one
  // thing a chart cannot leave unsaid, and the axis titles cannot say it: they
  // name the sides, not the lines, and the rows of marks have no axis at all.
  chart_key_ = new QLabel(this);
  chart_key_->setTextFormat(Qt::RichText);
  chart_key_->setContentsMargins(8, 4, 8, 0);
  connect(chart_, &SeasonChart::keyChanged, chart_key_, &QLabel::setText);

  // One box per quantity, in the same order the chart draws them. The first two
  // are ticked because they are the pair a farm is usually read by: what is
  // growing, and whether there is water for it.
  auto* picker = new QHBoxLayout;
  picker->setContentsMargins(8, 2, 8, 0);
  picker->addWidget(new QLabel("Plot", this));
  for (const ChartSeries& series : kChartSeries) {
    auto* box = new QCheckBox(series.label, this);
    box->setToolTip(QString("%1, as a mean over the farm each day.\n\nTwo at a time: choosing a "
                            "third turns off whichever was chosen first, so each axis can carry "
                            "one quantity with its own name and its own range.")
                        .arg(series.name));
    const std::size_t which = chart_boxes_.size();
    connect(box, &QCheckBox::toggled, this,
            [this, which](bool wanted) { choose_series(which, wanted); });
    chart_boxes_.push_back(box);
    picker->addWidget(box);
  }
  // Ticked after the boxes exist, so the opening pair goes through the same
  // path a person clicking would rather than a second one that has to agree.
  for (std::size_t i = 0; i < kChartSeries.size() && i < chart_boxes_.size(); ++i) {
    if (kChartSeries.at(i).on) {
      chart_boxes_[i]->setChecked(true);
    }
  }
  // Not one of the two: a row of marks reads off no axis, so it costs neither
  // of them. It is a box all the same, because everything on the chart should
  // be something that was chosen.
  irrigated_days_box_ = new QCheckBox("Irrigated days", this);
  irrigated_days_box_->setChecked(true);
  irrigated_days_box_->setToolTip(
      "Marks the days water was put on, under the plot.\n\nMarks rather than a line: a day was "
      "irrigated or it was not, and a line joining the days it happened on would slope through "
      "the days between - water on days that had none.");
  connect(irrigated_days_box_, &QCheckBox::toggled, this, [this] { refresh_chart(); });
  picker->addWidget(irrigated_days_box_);

  picker->addStretch(1);

  auto* chart_column = new QVBoxLayout;
  chart_column->setContentsMargins(0, 0, 0, 0);
  chart_column->setSpacing(0);
  chart_column->addLayout(picker);
  chart_column->addWidget(chart_key_);
  chart_column->addWidget(chart_, 1);
  auto* chart_holder = new QWidget(this);
  chart_holder->setLayout(chart_column);

  auto* lower = new QSplitter(Qt::Horizontal, this);
  lower->addWidget(readings_holder);
  lower->addWidget(chart_holder);
  lower->setSizes({kOpeningReadingsWidth, kOpeningChartWidth});

  auto* split = new QSplitter(Qt::Vertical, this);
  split->addWidget(scene_holder);
  split->addWidget(lower);
  split->setStretchFactor(0, 3);
  split->setStretchFactor(1, 1);
  // Stretch alone is not enough: a splitter starts from its children's size
  // hints, and a render window asks for far less than a farm needs, so the map
  // opened shorter than the strip under it.
  scene_holder->setMinimumHeight(kSmallestMapHeight);
  scene_split_ = split;

  auto* layout = new QVBoxLayout;
  layout->addWidget(split, 1);
  layout->addLayout(playing);
  layout->addLayout(choices);
  layout->addLayout(layers);

  // The two floating notices. Children of the window rather than of any layout,
  // so they sit over the map instead of pushing it about - a banner that
  // resized the scene every time a run started would be its own kind of jump.
  progress_label_ = new QLabel(this);
  progress_label_->setStyleSheet(
      "background: rgba(28, 34, 46, 235); color: #dfe6f2; border-radius: 6px; padding: 7px 13px;");
  progress_label_->hide();

  notice_label_ = new QLabel(this);
  notice_label_->hide();

  notice_timer_ = new QTimer(this);
  notice_timer_->setSingleShot(true);
  notice_timer_->setInterval(kNoticeSeconds * 1000);
  connect(notice_timer_, &QTimer::timeout, notice_label_, &QLabel::hide);

  view_->installEventFilter(this);

  auto* central = new QWidget(this);
  central->setLayout(layout);
  setCentralWidget(central);

  // The setup panel docks rather than opening as a dialog, so the map stays
  // visible while a run is being set up. A farm is chosen by looking at it.
  setup_ = new SetupPanel(data_directory_, this);

  // **The panel, then the two buttons, then the scenarios.**
  //
  // Top to bottom is the order somebody works in: set the farm up, keep that
  // setup as a scenario, and see the list of what has been kept. The list takes
  // a fifth of the height and the panel the rest, because the panel is where
  // the work is and the list is a record of it - and a list given equal room
  // would be four empty rows most of the time.
  add_scenario_button_ = new QPushButton("Add Scenario", this);
  add_scenario_button_->setToolTip(
      "Keeps the panel as it stands now, under a name you give it.\n\nUp to " +
      QString::number(kMostScenarios) +
      " scenarios; the comparison runs every one of them on the same weather.");

  // **One button runs whatever is in the list.**
  //
  // One scenario is a simulation and several are a comparison, but that is a
  // difference in what comes out rather than in what the person is doing, so it
  // is one button. Two - a Run and a Run Comparison - would leave somebody with
  // a single scenario looking at a greyed-out button and wondering what they
  // had done wrong.
  // **The scenario row is about the list, and says so.** It used to carry a
  // button called Run, which on a one-scenario list meant "run that one" and on
  // a longer one meant "compare them" - two different actions behind one word.
  // Running one scenario is the panel's job now.
  compare_button_ = new QPushButton("Run comparison", this);
  compare_button_->setToolTip(
      "Runs every scenario in the list and puts the results in one table.\n\nNeeds two: one "
      "scenario is a simulation, and the panel above already runs that. Each is a full year "
      "over the same recorded weather, so a difference between them is a difference between "
      "the rules rather than between the seasons they met.");
  compare_button_->setEnabled(false);

  report_button_ = new QPushButton("Comparison report", this);
  report_button_->setToolTip(
      "Opens the report on the last comparison: the table, what differed between the scenarios, "
      "and what the numbers say.\n\nFrom there it can be saved as a PDF or a CSV, or copied as "
      "Markdown. The report on a single run is under the readings beside the chart.");
  report_button_->setEnabled(false);

  scenario_list_ = new QListWidget(this);
  scenario_list_->setToolTip(
      "Choose one to load it back into the panel and draw it on the map, so a row of the "
      "comparison can be looked at rather than only read.");

  auto* scenario_buttons = new QHBoxLayout;
  scenario_buttons->addWidget(add_scenario_button_);
  scenario_buttons->addStretch(1);
  scenario_buttons->addWidget(compare_button_);
  scenario_buttons->addWidget(report_button_);

  auto* side = new QVBoxLayout;
  side->setContentsMargins(0, 0, 0, 0);
  side->addWidget(setup_, kPanelShare);
  side->addLayout(scenario_buttons);
  side->addWidget(scenario_list_, kScenarioListShare);

  auto* side_panel = new QWidget(this);
  side_panel->setLayout(side);

  connect(add_scenario_button_, &QPushButton::clicked, this, &MapWindow::add_scenario);
  connect(compare_button_, &QPushButton::clicked, this, &MapWindow::run_comparison);
  connect(report_button_, &QPushButton::clicked, this, &MapWindow::open_comparison_report);
  connect(setup_, &SetupPanel::readinessChanged, this, &MapWindow::refresh_scenario_list);
  connect(setup_, &SetupPanel::resultsReady, results_label_, &QLabel::setText);
  connect(setup_, &SetupPanel::readinessChanged, this,
          [this] { run_report_button_->setEnabled(setup_->can_report()); });
  connect(scenario_list_, &QListWidget::currentRowChanged, this, &MapWindow::show_scenario);

  auto* dock = new QDockWidget("Run a scenario", this);
  dock->setWidget(side_panel);
  dock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
  addDockWidget(Qt::LeftDockWidgetArea, dock);

  spray_timer_ = new QTimer(this);
  spray_timer_->setInterval(kSprayFrameInterval);
  connect(spray_timer_, &QTimer::timeout, this, [this] {
    spray_phase_ = std::fmod(spray_phase_ + kSprayPhaseStep, 1.0);
    terrain_.set_spray_phase(spray_phase_);
    if (showing_terrain_) {
      view_->renderWindow()->Render();
    }
  });

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
  connect(setup_, &SetupPanel::scenarioChanged, this, [this](const QString& directory) {
    // Choosing a farm runs it. Anything thrown here has to be caught rather
    // than escaping through the signal, where it would end the process instead
    // of appearing in the panel.
    try {
      open_scenario(directory.toStdString());
    } catch (const std::exception& error) {
      last_failure_ = error.what();
      setup_->show_failure(QString::fromUtf8(error.what()));
    }
  });

  // Open on the bundle named on the command line, configured as that bundle
  // configures itself. Pressing Run without touching anything therefore
  // reproduces the scenario as written, which is what makes the panel safe to
  // explore from: the user can always get back to the published run.
  const int head = bundle.mobs.empty() ? 0 : bundle.mobs.front().head;
  const double liveweight = bundle.mobs.empty() ? 0.0 : bundle.mobs.front().liveweight_kg;
  setup_->adopt_bundle(bundle_directory, head, liveweight,
                       bundle.management.has_value() ? &*bundle.management : nullptr,
                       bundle.mobs.empty() ? nullptr : &bundle.mobs.front().animal);

  // The dock opens wider than its own minimum, so the form has room before
  // anything has to scroll. Qt sizes a dock from its widget's hint otherwise,
  // and that hint is the minimum - which is the width at which the panel is
  // merely legible rather than comfortable.
  resizeDocks({dock}, {kOpeningPanelWidth}, Qt::Horizontal);

  // Wide enough for the panel beside the map, and a floor under it so the
  // window cannot be dragged narrower than the two of them together. The
  // controls no longer set this, because they are on two rows now and the
  // timeline gives up whatever width it is left.
  setMinimumWidth(kOpeningPanelWidth + 620);
  resize(kOpeningPanelWidth + 1080, 940);

  // **Opens in three dimensions.** The farm is a piece of country with layers
  // under it, and that is what the window is for; the flat map is the view to
  // switch to when a value has to be read off a legend exactly, which is the
  // narrower job. It is set before the first run so the opening frame is the
  // one that will be kept, rather than a flat map that redraws itself.
  view_box_->setCurrentIndex(1);

  // Now that the window has its size, give the map its share of it. Done here
  // rather than with fixed pixels so the proportion holds on a laptop and on a
  // large display alike.
  const int usable = height() - kControlsAllowance;
  const auto for_map = static_cast<int>(usable * kMapShareOfHeight);
  scene_split_->setSizes({std::max(for_map, kSmallestMapHeight), usable - for_map});

  start_run();
  scene_.reset_camera();
}

MapWindow::RunProducts MapWindow::simulate(const SetupPanel::Choices& choices) {
  // **Nothing here touches the window.** This runs on a worker thread, so every
  // result goes into the products it returns and the interface adopts them when
  // it is finished. A day callback that wrote into the window's own vectors -
  // which is what this used to do - was safe only because the run blocked
  // everything, and blocking everything is the lag being fixed.
  RunProducts products;
  try {
    config::ScenarioBundle bundle = config::load_scenario(choices.scenario_directory);
    products.no_ground_reason = attach_elevation(bundle, choices.scenario_directory);
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
    const bool measured_ground = bundle.terrain.kind == config::TerrainSpec::Kind::Snapshot;
    if (!measured_ground) {
      bundle.terrain = choices.terrain;
    }

    products.elevation = bundle.make_elevation();
    products.had_stock = !bundle.mobs.empty();
    if (products.had_stock) {
      simulate_managed(products, bundle, choices.policy, choices.irrigation,
                       choices.irrigation_system);
    } else {
      simulate_pasture_only(products, bundle, choices.irrigation, choices.irrigation_system);
    }

    products.policy = choices.policy;
    products.latitude_degrees = bundle.latitude_degrees;

    // The slope of the ground the run was over, taken once. Flat ground has no
    // topography, and a farm with none gets a raster of zeros rather than a
    // missing field: "every slope here is zero" is the true answer, and a menu
    // whose entries come and go with the scenario is a menu people stop
    // trusting.
    if (const std::optional<core::Topography> ground = bundle.make_topography();
        ground.has_value()) {
      products.slope.push_back(ground->slope_degrees);
    } else {
      core::GeoTransform transform;
      transform.origin_easting = bundle.grid->origin_easting;
      transform.origin_northing = bundle.grid->origin_northing;
      transform.cell_size = bundle.grid->cell_size_m;
      products.slope.emplace_back(bundle.grid->cols, bundle.grid->rows, transform, 0.0);
    }

    if (products.summary.has_value()) {
      products.weather = products.summary->weather;
      products.irrigation_mm = products.summary->irrigation_mm;
      products.irrigation = products.summary->irrigation;
    }
    products.measured_ground = measured_ground;
    products.bundle = std::move(bundle);
  } catch (const std::exception& error) {
    products.failure = error.what();
  }
  return products;
}

void MapWindow::start_run() {
  const SetupPanel::Choices choices = setup_->choices();
  if (choices.scenario_directory.empty()) {
    return;
  }
  if (running_) {
    // Asked for again while one is in flight. Remembered, not refused: see
    // rerun_wanted_.
    rerun_wanted_ = true;
    return;
  }

  timer_->stop();
  play_button_->setText("Play");
  setup_->set_running(true);
  running_ = true;
  refresh_scenario_list();
  show_progress("Running " + QString::fromStdString(choices.scenario_directory) + "...");

  // **On a worker, so the window keeps drawing while a year is simulated.**
  // Half a second is long enough for a click to feel ignored and for a switch
  // between scenarios to look like a hang.
  auto* watcher = new QFutureWatcher<RunProducts>(this);
  connect(watcher, &QFutureWatcher<RunProducts>::finished, this, [this, watcher] {
    adopt_run(watcher->result());
    watcher->deleteLater();
  });
  watcher->setFuture(QtConcurrent::run(&MapWindow::simulate, choices));
}

void MapWindow::adopt_run(RunProducts products) {
  running_ = false;
  setup_->set_running(false);

  // Something changed while this was running, so this answer is already out of
  // date. Drop it and run again rather than drawing it.
  if (rerun_wanted_) {
    rerun_wanted_ = false;
    start_run();
    return;
  }

  if (!products.failure.empty()) {
    // A failed run must not leave half a year on the timeline. Everything the
    // view draws from is cleared, so what is on screen is either a whole run or
    // nothing at all.
    clear_series();
    last_run_.reset();
    adopt_series();
    last_failure_ = products.failure;
    setup_->show_failure(QString::fromStdString(products.failure));
    hide_progress();
    announce("That scenario could not be run", false);
    refresh_scenario_list();
    return;
  }

  clear_series();
  cover_ = std::move(products.cover);
  soil_water_ = std::move(products.soil_water);
  available_water_ = std::move(products.available_water);
  water_stress_ = std::move(products.water_stress);
  irrigation_today_ = std::move(products.irrigation_today);
  irrigation_to_date_ = std::move(products.irrigation_to_date);
  growth_ = std::move(products.growth);
  legume_fraction_ = std::move(products.legume_fraction);
  slope_ = std::move(products.slope);
  dates_ = std::move(products.dates);
  mean_cover_ = std::move(products.mean_cover);
  stock_summary_ = std::move(products.stock_summary);
  grazed_each_day_ = std::move(products.grazed_each_day);
  mobs_each_day_ = std::move(products.mobs_each_day);
  boundaries_ = std::move(products.boundaries);
  paddocks_ = std::move(products.paddocks);
  weather_ = std::move(products.weather);
  irrigation_mm_ = std::move(products.irrigation_mm);
  irrigation_tally_ = products.irrigation;
  last_run_ = std::move(products.summary);
  last_bundle_ = std::move(products.bundle);
  elevation_ = std::move(products.elevation);
  last_policy_ = products.policy;
  latitude_degrees_ = products.latitude_degrees;
  last_run_had_stock_ = products.had_stock;
  no_ground_reason_ = std::move(products.no_ground_reason);
  last_failure_.clear();
  setup_->show_measured_ground(products.measured_ground);

  // Who owns which cell. Built here rather than during the run because the
  // fences do not move, and from the run's own raster so that the mask is over
  // exactly the cells the model stepped.
  mask_.reset();
  if (!paddocks_.empty() && !cover_.empty()) {
    mask_.emplace(cover_.front(), paddocks_);
  }
  selected_paddock_.reset();

  adopt_series();
  refresh_chart();
  if (last_run_.has_value() && last_bundle_.has_value() && last_bundle_->grid.has_value()) {
    const double hectares =
        static_cast<double>(last_bundle_->grid->cols * last_bundle_->grid->rows) *
        last_bundle_->grid->cell_size_m * last_bundle_->grid->cell_size_m / 10000.0;
    setup_->show_results(*last_run_, last_run_had_stock_, irrigation_tally_, hectares);
  }

  hide_progress();
  refresh_scenario_list();
  announce(QString("%1 days simulated").arg(dates_.size()), true);
}

void MapWindow::open_report() {
  if (!last_run_.has_value() || !last_bundle_.has_value() || !last_run_had_stock_) {
    return;
  }
  config::ReportOptions options;
  options.farm_name = last_bundle_->name;
  options.policy = &last_policy_;
  options.ground_caveat = no_ground_reason_;

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

bool MapWindow::save_window_screenshot(const std::string& path) {
  // **Let the deferred layouts run first.**
  //
  // A QChart lays itself out on the next turn of the event loop, so a grab
  // taken straight after the data changed captures the layout from before it -
  // the axes and title of a chart that no longer exists. That cost an afternoon
  // of looking for a bug in the chart: the chart was right and the picture of
  // it was one step behind.
  QCoreApplication::processEvents(QEventLoop::AllEvents, kWaitSliceMs);
  // The map itself is drawn by OpenGL into a surface Qt does not own, so a
  // grab of the window leaves a hole where it is. That is the point here: what
  // is being checked is everything around the map.
  return grab().save(QString::fromStdString(path), "PNG");
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

void MapWindow::show_configuration(int ground, bool terrain, int heights, bool irrigate) {
  setup_->select_ground(ground);
  setup_->select_irrigation(irrigate);
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
  const bool terrain = index == 1;
  if (terrain == showing_terrain_) {
    return;
  }
  showing_terrain_ = terrain;
  height_box_->setEnabled(terrain);
  pan_slider_->setEnabled(terrain);
  // The layers are a section through a three-dimensional scene. The flat map
  // has no profile to stack, so the row greys out rather than offering
  // switches that would do nothing.
  for (QCheckBox* box : layer_boxes_) {
    box->setEnabled(terrain);
  }

  vtkRenderWindow* window = view_->renderWindow();
  if (window != nullptr) {
    window->RemoveRenderer(showing_terrain_ ? scene_.renderer() : terrain_.renderer());
    window->AddRenderer(showing_terrain_ ? terrain_.renderer() : scene_.renderer());
  }
  refresh();
  if (showing_terrain_) {
    terrain_.reset_camera();
    // The framing is the view the slider calls zero, so the control has to say
    // so - left where it was it would sit somewhere it no longer means.
    pan_slider_->setValue(0);
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
  weather_.clear();
  slope_.clear();
  available_water_.clear();
  irrigation_today_.clear();
  irrigation_to_date_.clear();
  growth_.clear();
  paddocks_.clear();
  mask_.reset();
  selected_paddock_.reset();
  irrigation_mm_.clear();
  irrigation_tally_ = {};
  cover_.clear();
  soil_water_.clear();
  water_stress_.clear();
  legume_fraction_.clear();
  dates_.clear();
  mean_cover_.clear();
  whole_run_ranges_.clear();
  boundaries_.clear();
  grazed_each_day_.clear();
  mobs_each_day_.clear();
  stock_summary_.clear();
  elevation_.reset();
}

const std::vector<viz::MobMarker>& MapWindow::mobs_on(std::size_t day) const {
  return day < mobs_each_day_.size() ? mobs_each_day_[day] : kNobodyOnTheFarm;
}

const std::vector<std::size_t>& MapWindow::grazed_on(std::size_t day) const {
  return day < grazed_each_day_.size() ? grazed_each_day_[day] : kNothingGrazed;
}

void MapWindow::keep_day(RunProducts& into, const core::FarmletGrid& grid,
                         const std::string& date) {
  into.cover.push_back(grid.cover_kg_dm());
  into.soil_water.push_back(grid.soil_water_mm());
  into.available_water.push_back(grid.available_water_fraction());
  into.water_stress.push_back(grid.water_stress());

  // Today's water, and the running total behind it. The total is accumulated
  // here rather than asked of the grid, because the grid holds a day and not a
  // season - it would have to keep a tally for a picture, which is the wrong
  // reason for a model to remember anything.
  core::Raster<double> today = grid.last_irrigation_mm();
  core::Raster<double> so_far = today;
  if (!into.irrigation_to_date.empty()) {
    const core::Raster<double>& before = into.irrigation_to_date.back();
    for (std::size_t cell = 0; cell < so_far.size() && cell < before.size(); ++cell) {
      so_far.values()[cell] += before.values()[cell];
    }
  }
  into.growth.push_back(grid.last_growth_kg_dm());
  into.irrigation_today.push_back(std::move(today));
  into.irrigation_to_date.push_back(std::move(so_far));

  into.legume_fraction.push_back(grid.legume_fraction());
  into.dates.push_back(date);
  into.mean_cover.push_back(grid.mean_cover_kg_dm());
}

void MapWindow::simulate_managed(RunProducts& into, const config::ScenarioBundle& bundle,
                                 const core::ManagementPolicy& policy,
                                 const core::IrrigationPolicy& irrigation,
                                 const core::IrrigationSystem& system) {
  core::DietQuality diet;
  diet.metabolisable_energy_mj_per_kg_dm = kPastureMe;
  diet.digestibility_percent = kPastureDigestibility;

  into.summary = config::run_managed_scenario(
      bundle, policy, diet, bundle.name,
      [&into](const core::Farm& farm, const core::FarmDay& day) {
        keep_day(into, farm.grid(), day.date.to_iso_string());

        // The fences do not move, so they are taken once, on the first day.
        if (into.boundaries.empty()) {
          into.boundaries.reserve(farm.paddocks().size());
          for (const core::Paddock& paddock : farm.paddocks()) {
            into.boundaries.push_back(paddock.boundary);
          }
          into.paddocks = farm.paddocks();
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
        into.grazed_each_day.push_back(std::move(grazed));

        // Where the stock stood, one marker per paddock each mob occupied. A
        // set stocked mob has the run of the farm and gets a mark on all of it,
        // which is what set stocking looks like; a rotating one gets one mark.
        std::vector<viz::MobMarker> markers;
        for (const core::FarmMob& mob : farm.mobs()) {
          // How many of the mob to draw in each paddock it has the run of.
          // Split evenly, with the remainder going to the first paddocks, and
          // it is an illustration: nothing in the model says how a set stocked
          // mob distributes itself. marker.head stays the whole mob, because
          // that is the number the model actually knows.
          const auto occupied = static_cast<int>(mob.paddocks.size());
          int drawn = 0;
          for (const std::size_t paddock : mob.paddocks) {
            if (paddock >= farm.paddocks().size()) {
              continue;
            }
            viz::MobMarker marker;
            marker.at = farm.paddocks()[paddock].boundary.centroid();
            marker.kind = mob.mob.animal.kind;
            marker.head = mob.mob.head;
            marker.paddock = paddock;
            marker.head_here = occupied > 0 ? (mob.mob.head / occupied) +
                                                  (drawn < (mob.mob.head % occupied) ? 1 : 0)
                                            : 0;
            ++drawn;
            markers.push_back(marker);
          }
        }
        into.mobs_each_day.push_back(std::move(markers));

        if (into.stock_summary.empty()) {
          std::string summary;
          for (const core::FarmMob& mob : farm.mobs()) {
            if (!summary.empty()) {
              summary += ", ";
            }
            summary += std::to_string(mob.mob.head) + " " + core::to_string(mob.mob.animal.kind) +
                       " (" + mob.mob.animal.class_id + ")";
          }
          into.stock_summary = summary;
        }
      },
      irrigation, system);
}

void MapWindow::simulate_pasture_only(RunProducts& into, const config::ScenarioBundle& bundle,
                                      const core::IrrigationPolicy& irrigation,
                                      const core::IrrigationSystem& system) {
  core::FarmletGrid grid = bundle.make_grid();
  const core::WeatherSeries weather = bundle.weather->fetch(bundle.range);

  config::RunSummary summary;
  summary.label = bundle.name;
  grid.set_opening_stocks(summary.ledger);

  const std::size_t days = weather.records.size();
  into.cover.reserve(days);
  into.soil_water.reserve(days);
  into.water_stress.reserve(days);
  into.legume_fraction.reserve(days);
  into.dates.reserve(days);
  into.mean_cover.reserve(days);

  // The schedule holds the per-cell memory of when each piece of ground was
  // last watered. It reads the grid's dryness and decides; the grid applies
  // what it is handed and decides nothing.
  core::IrrigationSchedule schedule(irrigation, system, grid.cell_count());
  into.irrigation_mm.reserve(days);

  for (const core::DailyWeather& day : weather.records) {
    const core::Raster<double> dryness = grid.depletion_mm();
    const std::vector<double>& water =
        schedule.decide(dryness.values(), grid.total_available_water_mm());
    into.irrigation_mm.push_back(schedule.last_mean_mm());
    grid.step(day, &summary.ledger, water);
    keep_day(into, grid, day.date.to_iso_string());
    summary.dates.push_back(day.date);
    summary.weather.push_back(day);
    summary.cover_kg_dm_per_ha.push_back(grid.mean_cover_kg_dm());
  }

  into.irrigation = schedule.tally();
  summary.closing_cover_kg_dm = grid.mean_cover_kg_dm();
  summary.closing_nitrogen_kg = grid.mean_total_nitrogen_kg();
  summary.closing_water_mm = grid.mean_soil_water_mm();
  into.summary = std::move(summary);
}

void MapWindow::open_scenario(const std::string& bundle_directory) {
  // Absolute, because the panel holds absolute paths and matches on them
  // exactly. Handed a relative one it finds nothing, changes nothing, and the
  // run that follows is the farm that was already there - which looks like
  // success from the outside.
  const std::string resolved =
      QDir(QString::fromStdString(bundle_directory)).absolutePath().toStdString();
  const config::ScenarioBundle bundle = config::load_scenario(resolved);
  setup_->adopt_bundle(resolved, bundle.mobs.empty() ? 0 : bundle.mobs.front().head,
                       bundle.mobs.empty() ? 0.0 : bundle.mobs.front().liveweight_kg,
                       bundle.management.has_value() ? &*bundle.management : nullptr,
                       bundle.mobs.empty() ? nullptr : &bundle.mobs.front().animal);
  if (setup_->choices().scenario_directory != resolved) {
    throw std::runtime_error("the panel does not offer " + resolved +
                             ", so nothing was opened. The panel lists the bundles under the data "
                             "directory it was given.");
  }
  // No reset_camera here, deliberately. Moving to the new farm is the run's own
  // job; doing it here as well would hide a failure to do it at all.
  start_run();
}

std::optional<std::pair<double, double>> MapWindow::camera_focus() const {
  vtkRenderer* renderer = showing_terrain_ ? terrain_.renderer() : scene_.renderer();
  if (renderer == nullptr || renderer->GetActiveCamera() == nullptr) {
    return std::nullopt;
  }
  const double* focus = renderer->GetActiveCamera()->GetFocalPoint();
  return std::make_pair(focus[0], focus[1]);
}

std::optional<std::array<double, 4>> MapWindow::drawn_farm() const {
  if (cover_.empty()) {
    return std::nullopt;
  }
  const core::Raster<double>& raster = cover_.front();
  const core::GeoTransform& transform = raster.transform();
  const double width = static_cast<double>(raster.cols()) * transform.cell_size;
  const double height = static_cast<double>(raster.rows()) * transform.cell_size;
  return std::array<double, 4>{transform.origin_easting, transform.origin_northing - height, width,
                               height};
}

bool MapWindow::farm_moved(const core::Raster<double>& raster) const {
  if (!drawn_extent_.has_value()) {
    return true;
  }
  const core::GeoTransform& before = *drawn_extent_;
  const core::GeoTransform& now = raster.transform();

  // Exact comparison, deliberately. These are not computed values that drift -
  // they are read from the manifest and passed through - so two farms are the
  // same farm when their extents are the same numbers. A tolerance here would
  // only invent a distance below which two farms count as one.
  return before.origin_easting != now.origin_easting ||
         before.origin_northing != now.origin_northing || before.cell_size != now.cell_size ||
         drawn_cols_ != raster.cols() || drawn_rows_ != raster.rows();
}

void MapWindow::adopt_series() {
  for (const Field field : {Field::Cover, Field::SoilWater, Field::AvailableWater,
                            Field::WaterStress, Field::IrrigationToday, Field::IrrigationToDate,
                            Field::Growth, Field::LegumeFraction, Field::Slope}) {
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

  // The terrain view is offered whatever the run was over. A farm with no
  // elevation is drawn as the flat thing it is, and the line under the map says
  // why - more use than a disabled control explained in a tooltip nobody hovers.
  view_box_->setToolTip(
      elevation_.has_value()
          ? QString("The ground this run was over.")
          : QString("This run has no measured ground, so the terrain view draws it flat."));

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

  // Follow the farm. A run over a different piece of ground needs the camera
  // moved to it; a run over the same ground keeps whatever view was being
  // used, because re-running with different stock is not a reason to throw
  // away somebody's zoom.
  if (!cover_.empty()) {
    if (farm_moved(cover_.front())) {
      if (showing_terrain_) {
        terrain_.reset_camera();
      } else {
        scene_.reset_camera();
      }
      if (vtkRenderWindow* window = view_->renderWindow(); window != nullptr) {
        window->Render();
      }
    }
    drawn_extent_ = cover_.front().transform();
    drawn_cols_ = cover_.front().cols();
    drawn_rows_ = cover_.front().rows();
  } else {
    drawn_extent_.reset();
  }
}

const std::vector<core::Raster<double>>& MapWindow::series_of(Field field) const {
  switch (field) {
    case Field::Slope:
      return slope_;
    case Field::SoilWater:
      return soil_water_;
    case Field::AvailableWater:
      return available_water_;
    case Field::IrrigationToday:
      return irrigation_today_;
    case Field::IrrigationToDate:
      return irrigation_to_date_;
    case Field::Growth:
      return growth_;
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
  if (series.size() < 2) {
    return 0;  // Nothing varies over a single frame.
  }
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
  if (series.empty()) {
    return;
  }
  // Slope has one frame and the rest have one per day, because the ground does
  // not move and the pasture does.
  const core::Raster<double>& raster = series[std::min(day, series.size() - 1)];
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

  // The day's sky, and the sun that lit it.
  //
  // 14:00 solar time, every day. The model has no hours - a DailyWeather is a
  // day's totals - so a single hour has to stand for the whole of it, and this
  // one earns the job: over New Zealand the sun is above the horizon at 2 pm on
  // every day of the year (17.5 degrees at midwinter, 58.2 at midsummer for
  // Lincoln), and it sits in the north-west rather than due north, so the
  // ground has a lit side and a shaded one. At solar noon the sun is exactly
  // north and the relief flattens out.
  const bool have_weather = day < weather_.size();
  const double clearness =
      have_weather ? core::clearness_index(weather_[day].solar_radiation_mj_per_m2,
                                           core::extraterrestrial_radiation_mj(
                                               latitude_degrees_, weather_[day].date.day_of_year()))
                   : 0.0;
  if (have_weather) {
  }
  show_weather(day, clearness);
  show_selected_paddock();
  chart_->mark_day(static_cast<int>(day));

  // Where today's water landed, drawn over the ground it landed on. The scene
  // is handed the depths and works out the picture; it decides no water.
  refresh_irrigation(day);

  // The soil under the pasture, on the day being shown. Handed the share of
  // available water left rather than the depth in millimetres: the profile is
  // drawn as a section and "how full is it" is the question a section answers,
  // where millimetres would need a legend the layer does not have.
  terrain_.name_top_layer(style.label);
  refresh_stack(day);

  const viz::ColourScale colours(style.ramp, lowest, highest);
  if (showing_terrain_) {
    // A run with no elevation is draped on a level surface of its own shape.
    // Zero rather than an invented datum: a made-up height is a number somebody
    // could read off the view and believe.
    if (!elevation_.has_value()) {
      flat_ground_ = core::Raster<double>(raster.cols(), raster.rows(), raster.transform(), 0.0);
    }
    const core::Raster<double>& ground = elevation_.has_value() ? *elevation_ : flat_ground_;
    terrain_.show(raster, ground, colours, legend);
    // **After the surface, not before.** The sky hangs off the farm's extent,
    // which show() is what works out - so called first it drew nothing, and the
    // camera, which frames whatever is visible, framed the farm alone and left
    // the weather to hang off the top of the picture.
    terrain_.light_the_ground();
    terrain_.show_sky(latitude_degrees_, weather_[day].date.day_of_year(), kSolarHourShown,
                      clearness, weather_[day].rainfall_mm, weather_[day].wind_speed_m_per_s,
                      weather_[day].uv_index);
    terrain_.show_grazed(grazed_on(day));
    terrain_.show_mobs(mobs_on(day));
  } else {
    scene_.show(raster, colours, legend);
    scene_.show_grazed(grazed_on(day));
    scene_.show_mobs(mobs_on(day));
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
  const QString stock =
      stock_summary_.empty() ? QString("no stock") : QString::fromStdString(stock_summary_);

  // Why the ground looks the way it does, when it is not what the scenario
  // asked for. This is the "quietly" that a farm running flat must not be.
  QString ground_note;
  if (!no_ground_reason_.empty()) {
    ground_note = "   |   " + QString::fromStdString(no_ground_reason_);
  } else if (showing_terrain_ && !elevation_.has_value()) {
    ground_note = "   |   drawn flat: this run was not over any terrain";
  }
  summary_label_->setText(
      QString("%5   |   Mean pasture cover %1 kg DM/ha over %2 cells of %7 m (%8 ha each)   |   "
              "%3 across the farm today: %4%6")
          .arg(mean_cover_[day], 0, 'f', 0)
          .arg(raster.size())
          .arg(style.label)
          .arg(spread > 0.0
                   ? QString("%1 to %2").arg(today.first, 0, 'f', 1).arg(today.second, 0, 'f', 1)
                   : QString("uniform (%1)").arg(today.first, 0, 'f', 1))
          .arg(stock)
          .arg(ground_note)
          .arg(raster.transform().cell_size, 0, 'f', 0)
          .arg(raster.transform().cell_size * raster.transform().cell_size / 10000.0, 0, 'f', 4));
  view_->renderWindow()->Render();
}

// **A click inspects; a drag still turns the scene.**
//
// The event is examined and passed on rather than consumed, because the same
// button spins the camera in the terrain view and taking it away would trade
// one useful thing for another. Only a press that goes nowhere - press and
// release within a few pixels - counts as asking about a paddock.
bool MapWindow::eventFilter(QObject* watched, QEvent* event) {
  if (watched == view_) {
    if (event->type() == QEvent::MouseButtonPress) {
      auto* press = dynamic_cast<QMouseEvent*>(event);
      if (press != nullptr && press->button() == Qt::LeftButton) {
        pressed_at_ = press->pos();
      }
    } else if (event->type() == QEvent::MouseButtonRelease) {
      auto* release = dynamic_cast<QMouseEvent*>(event);
      if (release != nullptr && release->button() == Qt::LeftButton &&
          (release->pos() - pressed_at_).manhattanLength() <= kClickSlopPixels) {
        inspect_at(release->pos().x(), release->pos().y());
      }
    }
  }
  return QMainWindow::eventFilter(watched, event);
}

std::optional<core::Point2D> MapWindow::ground_under(int x, int y) const {
  return showing_terrain_ ? terrain_.ground_at(x, y) : scene_.ground_at(x, y);
}

int MapWindow::render_width() const {
  vtkRenderWindow* window = view_ != nullptr ? view_->renderWindow() : nullptr;
  return window != nullptr ? window->GetSize()[0] : 0;
}

int MapWindow::render_height() const {
  vtkRenderWindow* window = view_ != nullptr ? view_->renderWindow() : nullptr;
  return window != nullptr ? window->GetSize()[1] : 0;
}

void MapWindow::go_to_day(int day) {
  if (dates_.empty()) {
    return;
  }
  timeline_->setValue(std::clamp(day, 0, static_cast<int>(dates_.size()) - 1));
}

void MapWindow::slide_view(int percent) {
  pan_slider_->setValue(std::clamp(percent, pan_slider_->minimum(), pan_slider_->maximum()));
}

void MapWindow::select_view(bool terrain) {
  view_box_->setCurrentIndex(terrain ? 1 : 0);
}

void MapWindow::refresh_irrigation(std::size_t day) {
  if (day >= irrigation_today_.size()) {
    return;
  }
  viz::TerrainScene::IrrigationToday today;
  today.applied_mm = irrigation_today_[day];
  // Averaged over the cells the mask gives each paddock, which is the same
  // partition the model grazed - a bar worked out over a different set of cells
  // would be a second answer to a question already answered.
  today.paddock_mm.assign(paddocks_.size(), 0.0);
  for (std::size_t index = 0; index < paddocks_.size(); ++index) {
    today.paddock_mm[index] = paddock_mean(irrigation_today_, index, day).value_or(0.0);
  }

  terrain_.show_irrigation(today);

  // The wave only runs when there is spray to run it over. A timer ticking
  // against an empty scene is work nobody asked for, and on a laptop it is
  // work somebody pays for.
  const bool anything = std::any_of(today.paddock_mm.begin(), today.paddock_mm.end(),
                                    [](double depth) { return depth > 0.0; });
  if (anything && showing_terrain_ && terrain_.spray_shown()) {
    if (!spray_timer_->isActive()) {
      spray_timer_->start();
    }
  } else if (spray_timer_->isActive()) {
    spray_timer_->stop();
  }
}

void MapWindow::choose_series(std::size_t which, bool wanted) {
  const auto already = std::find(chart_order_.begin(), chart_order_.end(), which);

  if (!wanted) {
    if (already != chart_order_.end()) {
      chart_order_.erase(already);
    }
    refresh_chart();
    return;
  }
  if (already != chart_order_.end()) {
    return;
  }

  chart_order_.push_back(which);
  while (chart_order_.size() > kMostPlotted) {
    const std::size_t oldest = chart_order_.front();
    chart_order_.erase(chart_order_.begin());
    // Unticked without coming back through here: the list is already correct,
    // and a second pass would undo the choice just made.
    if (oldest < chart_boxes_.size()) {
      const QSignalBlocker quiet(chart_boxes_[oldest]);
      chart_boxes_[oldest]->setChecked(false);
    }
  }
  refresh_chart();
}

void MapWindow::refresh_chart() {
  if (dates_.empty()) {
    chart_->clear();
    return;
  }

  std::vector<QString> dates;
  dates.reserve(dates_.size());
  for (const std::string& date : dates_) {
    dates.push_back(QString::fromStdString(date));
  }

  // The mean over the farm for each day. A chart of a whole run is about when,
  // and where is what the map above it is for.
  const auto mean_each_day = [](const std::vector<core::Raster<double>>& series) {
    std::vector<double> means;
    means.reserve(series.size());
    for (const core::Raster<double>& raster : series) {
      double total = 0.0;
      for (const double value : raster.values()) {
        total += value;
      }
      means.push_back(raster.empty() ? 0.0 : total / static_cast<double>(raster.size()));
    }
    return means;
  };

  // In the order they were chosen: the first gets the left axis and the second
  // the right, so any two quantities can be put beside each other.
  std::vector<SeasonChart::Line> lines;
  for (const std::size_t which : chart_order_) {
    if (which >= kChartSeries.size()) {
      continue;
    }
    const ChartSeries& wanted = kChartSeries.at(which);
    const std::vector<core::Raster<double>>& source = series_of(wanted.field);
    if (source.empty()) {
      continue;
    }
    lines.push_back({wanted.name, wanted.unit, QColor(wanted.red, wanted.green, wanted.blue),
                     mean_each_day(source)});
  }

  // **Events, not lines.** A day was irrigated or it was not, and a line
  // joining the days it happened on would slope through the days between - the
  // picture inventing water on days that had none. They need no axis, which is
  // why two carry every quantity above.
  std::vector<SeasonChart::Events> events;

  if (irrigated_days_box_ != nullptr && irrigated_days_box_->isChecked()) {
    std::vector<bool> watered(dates_.size(), false);
    for (std::size_t day = 0; day < irrigation_mm_.size() && day < watered.size(); ++day) {
      watered[day] = irrigation_mm_[day] > 0.0;
    }
    events.push_back({"Irrigated", QColor(60, 160, 235), std::move(watered)});
  }

  // **Mob moves are not drawn, and the reason is density.**
  //
  // Grazing was tried first and was a solid bar - under a rotation some paddock
  // is being grazed every day of the year. Moves were tried instead, and on
  // this farm the mob shifts about every third day: a hundred and twenty marks
  // across a chart a few hundred pixels wide is a solid bar again. A band that
  // is always full carries nothing, and the count it would have carried is in
  // the readings beside the chart, where it is a number rather than a texture.

  chart_->show_run(dates, lines, events);
  chart_->mark_day(current_day_);
}

void MapWindow::refresh_stack(std::size_t day) {
  std::vector<viz::TerrainScene::StackEntry> entries;
  entries.reserve(kStackedFields.size());
  for (const Field field : kStackedFields) {
    const std::vector<core::Raster<double>>& series = series_of(field);
    if (series.empty()) {
      continue;
    }
    const core::Raster<double>& raster = series[std::min(day, series.size() - 1)];
    const FieldStyle style = style_of(field);

    // **Each sheet keeps its own scale over the whole run.** These are
    // different quantities in different units, so one shared ramp would invite
    // a comparison that means nothing - and a sheet rescaled to its own day
    // would change colour while its values held still, which is the opposite of
    // what a stack watched over a year is for.
    std::pair<double, double> range =
        style.has_natural_range ? std::pair<double, double>{style.natural_low, style.natural_high}
                                : whole_run_range(field);
    if (range.second <= range.first) {
      range.second = range.first + 1.0;
    }
    entries.push_back(
        {raster, viz::ColourScale(style.ramp, range.first, range.second), style.label});
  }
  terrain_.show_stack(entries);
}

double MapWindow::irrigation_today_mm() const {
  const auto day = static_cast<std::size_t>(std::max(0, current_day_));
  return day < irrigation_mm_.size() ? irrigation_mm_[day] : 0.0;
}

void MapWindow::wait_for_run() const {
  while (running_) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, kWaitSliceMs);
  }
}

bool MapWindow::irrigation_animating() const {
  return spray_timer_ != nullptr && spray_timer_->isActive();
}

void MapWindow::refresh_scenario_list() {
  const QSignalBlocker quiet(scenario_list_);
  const int chosen = scenario_list_->currentRow();
  scenario_list_->clear();
  for (const StoredScenario& scenario : scenarios_) {
    scenario_list_->addItem(scenario.name);
  }
  if (chosen >= 0 && chosen < scenario_list_->count()) {
    scenario_list_->setCurrentRow(chosen);
  }

  add_scenario_button_->setEnabled(static_cast<int>(scenarios_.size()) < kMostScenarios &&
                                   setup_->ready());
  // Two or more, because one is not a comparison. The panel's own Run covers
  // the single case, so nothing is out of reach - which is what makes it fair
  // to disable this rather than have it mean something else.
  compare_button_->setEnabled(scenarios_.size() >= kFewestCompared);
  // A report needs a run behind it. Offering one before anything has been run
  // would open a window describing nothing.
  report_button_->setEnabled(last_report_.has_value());
}

void MapWindow::add_scenario() {
  if (static_cast<int>(scenarios_.size()) >= kMostScenarios) {
    return;
  }

  // **The person names it, and the name matters.** "Irrigated" and "as we farm
  // it now" carry what a scenario was for; "Scenario 2" carries nothing, and a
  // table of five of those cannot be read at all.
  bool named = false;
  const QString name = QInputDialog::getText(
                           this, "Add scenario", "What is this scenario called?", QLineEdit::Normal,
                           QString("Scenario %1").arg(scenarios_.size() + 1), &named)
                           .trimmed();
  if (!named || name.isEmpty()) {
    return;
  }

  StoredScenario scenario;
  scenario.name = name;
  scenario.choices = setup_->choices();
  scenario.settings = setup_->describe();
  scenarios_.push_back(std::move(scenario));

  refresh_scenario_list();
  scenario_list_->setCurrentRow(static_cast<int>(scenarios_.size()) - 1);
}

void MapWindow::show_scenario(int index) {
  if (index < 0 || index >= static_cast<int>(scenarios_.size())) {
    return;
  }
  // Loaded back into the panel and run, so the map beside the list is showing
  // the scenario the list has selected - a row of a comparison can be looked at
  // rather than only read.
  setup_->adopt_choices(scenarios_[static_cast<std::size_t>(index)].choices);
  start_run();
}

std::vector<config::ComparedScenario> MapWindow::run_scenarios(
    QString& failure, std::vector<std::string>& flat_ground) {
  // **Every scenario is run again now, rather than remembered from when it was
  // added.** A stored result would go stale the moment the bundle or the
  // weather changed underneath it, and a table of five answers from five
  // different versions of the model is worse than no table. A year over this
  // farm is under half a second, so there is nothing to save by keeping them.
  QApplication::setOverrideCursor(Qt::WaitCursor);
  std::vector<config::ComparedScenario> compared;
  failure.clear();
  for (StoredScenario& scenario : scenarios_) {
    try {
      // **Loaded exactly as a single run loads it, elevation and all.**
      // config::load_scenario alone leaves a farm that names a survey with no
      // reader for it, and the bundle then refuses to run - which is right for
      // a library and wrong here, where the window already knows how to attach
      // one and how to fall back to flat when the file is not on this machine.
      config::ScenarioBundle bundle = config::load_scenario(scenario.choices.scenario_directory);
      if (const std::string reason = attach_elevation(bundle, scenario.choices.scenario_directory);
          !reason.empty()) {
        flat_ground.push_back(scenario.name.toStdString() + " ran on flat ground: " + reason);
      }
      const double hectares = bundle.grid.has_value()
                                  ? static_cast<double>(bundle.grid->cols * bundle.grid->rows) *
                                        bundle.grid->cell_size_m * bundle.grid->cell_size_m /
                                        10000.0
                                  : 0.0;

      core::DietQuality diet;
      diet.metabolisable_energy_mj_per_kg_dm = kPastureMe;
      diet.digestibility_percent = kPastureDigestibility;

      config::ComparedScenario entry;
      entry.name = scenario.name.toStdString();
      entry.hectares = hectares;
      entry.settings = scenario.settings;
      entry.summary = config::run_managed_scenario(bundle, scenario.choices.policy, diet,
                                                   entry.name, nullptr, scenario.choices.irrigation,
                                                   scenario.choices.irrigation_system);
      scenario.hectares = hectares;
      scenario.result = entry.summary;
      compared.push_back(std::move(entry));
    } catch (const std::exception& error) {
      failure = QString("%1 could not be run: %2").arg(scenario.name).arg(error.what());
      break;
    }
  }
  QApplication::restoreOverrideCursor();

  return compared;
}

void MapWindow::run_comparison() {
  if (scenarios_.empty()) {
    return;
  }
  QString failure;
  std::vector<std::string> flat_ground;
  const std::vector<config::ComparedScenario> compared = run_scenarios(failure, flat_ground);
  if (!failure.isEmpty()) {
    QMessageBox::warning(this, "Comparison", failure);
    return;
  }

  config::ComparisonTable table = config::compare(compared);
  // Under the table with the rest of what it cannot say. A farm that ran
  // without its measured ground is still comparable with one that did, but the
  // reader has to be told which happened.
  for (std::string& reason : flat_ground) {
    table.caveats.push_back(std::move(reason));
  }
  last_report_ = std::move(table);
  refresh_scenario_list();

  // The scenario the list has selected is the one drawn, so the map agrees with
  // whichever row somebody is looking at.
  if (scenario_list_->currentRow() < 0 && !scenarios_.empty()) {
    scenario_list_->setCurrentRow(0);
  } else {
    show_scenario(scenario_list_->currentRow());
  }

  open_comparison_report();
}

void MapWindow::open_comparison_report() {
  if (!last_report_.has_value()) {
    return;
  }
  ComparisonDialog dialog(*last_report_, this);
  dialog.exec();
}

void MapWindow::select_irrigation(bool on) {
  setup_->select_irrigation(on);
}

void MapWindow::keep_scenario(const QString& name) {
  if (static_cast<int>(scenarios_.size()) >= kMostScenarios) {
    return;
  }
  StoredScenario scenario;
  scenario.name = name;
  scenario.choices = setup_->choices();
  scenario.settings = setup_->describe();
  scenarios_.push_back(std::move(scenario));
  refresh_scenario_list();
}

std::string MapWindow::comparison_markdown(QString& failure) {
  failure.clear();
  if (scenarios_.empty()) {
    failure = "there are no scenarios to run";
    return {};
  }
  std::vector<std::string> flat_ground;
  const std::vector<config::ComparedScenario> compared = run_scenarios(failure, flat_ground);
  if (!failure.isEmpty()) {
    return {};
  }
  config::ComparisonTable table = config::compare(compared);
  for (std::string& reason : flat_ground) {
    table.caveats.push_back(std::move(reason));
  }
  return config::as_markdown(table) + "\n" + config::summarise(table) + "\n";
}

void MapWindow::show_progress(const QString& what) {
  progress_label_->setText(what);
  progress_label_->adjustSize();
  place_notices();
  progress_label_->show();
  progress_label_->raise();
}

void MapWindow::hide_progress() {
  progress_label_->hide();
}

void MapWindow::announce(const QString& what, bool good) {
  notice_label_->setText(what);
  // Green for done, amber for not. Colour alone would be a poor signal, so the
  // words say it too - the colour is only there to be seen before they are
  // read.
  notice_label_->setStyleSheet(
      good ? "background: rgba(38, 132, 84, 235); color: white; border-radius: 6px; padding: 7px "
             "13px; font-weight: 600;"
           : "background: rgba(176, 108, 22, 235); color: white; border-radius: 6px; padding: 7px "
             "13px; font-weight: 600;");
  notice_label_->adjustSize();
  place_notices();
  notice_label_->show();
  notice_label_->raise();
  notice_timer_->start();
}

void MapWindow::place_notices() {
  if (view_ == nullptr) {
    return;
  }
  // Positioned against the map rather than the window, so a note about a run
  // sits over the thing the run drew.
  const QPoint corner = view_->mapTo(this, QPoint(view_->width(), 0));
  notice_label_->move(corner.x() - notice_label_->width() - kNoticeMargin,
                      corner.y() + kNoticeMargin);
  const QPoint left = view_->mapTo(this, QPoint(0, 0));
  progress_label_->move(left.x() + kNoticeMargin, left.y() + kNoticeMargin);
}

void MapWindow::resizeEvent(QResizeEvent* event) {
  QMainWindow::resizeEvent(event);
  place_notices();
}

void MapWindow::show_all_layers() {
  for (QCheckBox* box : layer_boxes_) {
    box->setChecked(true);
  }
}

std::string MapWindow::inspect_pixel(int x, int y) {
  const std::optional<core::Point2D> ground =
      showing_terrain_ ? terrain_.ground_at(x, y) : scene_.ground_at(x, y);
  if (!ground.has_value()) {
    return "that point missed the farm";
  }
  selected_paddock_.reset();
  for (std::size_t i = 0; i < paddocks_.size(); ++i) {
    if (paddocks_[i].boundary.contains(*ground)) {
      selected_paddock_ = i;
      break;
    }
  }
  show_selected_paddock();
  // The label carries markup for the window; a caller reading it wants the
  // words.
  QString plain = paddock_label_->text();
  plain.replace("&nbsp;", " ");
  plain.remove(QRegularExpression("<[^>]*>"));
  return plain.simplified().toStdString();
}

void MapWindow::inspect_at(int x, int y) {
  if (view_ == nullptr || !mask_.has_value() || paddocks_.empty()) {
    return;
  }

  // Qt measures y down from the top of the widget and VTK measures it up from
  // the bottom, and both work in device pixels while Qt hands out logical ones.
  // Getting either wrong picks a point that is plausibly on the farm and is not
  // the one under the cursor, which is the hardest kind of mistake to see.
  const double ratio = view_->devicePixelRatioF();
  const int device_x = static_cast<int>(std::lround(x * ratio));
  const int device_y = static_cast<int>(std::lround((view_->height() - y) * ratio));

  const std::optional<core::Point2D> ground = showing_terrain_
                                                  ? terrain_.ground_at(device_x, device_y)
                                                  : scene_.ground_at(device_x, device_y);
  if (!ground.has_value()) {
    selected_paddock_.reset();
    paddock_label_->setText("<b>Paddock</b> &nbsp; that click missed the farm");
    return;
  }

  // Asked of the boundaries rather than of the mask, because a click lands on a
  // point and the mask answers for whole cells: near a fence the two disagree,
  // and the honest answer to "what did I click on" is the paddock the point is
  // actually inside.
  selected_paddock_.reset();
  for (std::size_t i = 0; i < paddocks_.size(); ++i) {
    if (paddocks_[i].boundary.contains(*ground)) {
      selected_paddock_ = i;
      break;
    }
  }
  show_selected_paddock();
}

std::optional<double> MapWindow::paddock_mean(const std::vector<core::Raster<double>>& series,
                                              std::size_t paddock, std::size_t day) const {
  if (series.empty() || !mask_.has_value()) {
    return std::nullopt;
  }
  const core::Raster<double>& raster = series[std::min(day, series.size() - 1)];
  double total = 0.0;
  std::size_t counted = 0;
  for (std::size_t row = 0; row < raster.rows(); ++row) {
    for (std::size_t col = 0; col < raster.cols(); ++col) {
      if (mask_->owner(col, row) == paddock) {
        total += raster(col, row);
        ++counted;
      }
    }
  }
  if (counted == 0) {
    return std::nullopt;
  }
  return total / static_cast<double>(counted);
}

void MapWindow::show_selected_paddock() {
  if (!selected_paddock_.has_value() || !mask_.has_value()) {
    paddock_label_->setText("<b>Paddock</b> &nbsp; click the map to inspect one");
    return;
  }
  const std::size_t index = *selected_paddock_;
  if (index >= paddocks_.size()) {
    return;
  }
  const auto day = static_cast<std::size_t>(
      std::clamp(current_day_, 0, static_cast<int>(dates_.empty() ? 0 : dates_.size() - 1)));

  const auto figure = [](const std::optional<double>& value, int places,
                         const char* suffix) -> QString {
    return value.has_value() ? QString("%1%2").arg(*value, 0, 'f', places).arg(suffix)
                             : QString("-");
  };
  const std::optional<double> left = paddock_mean(available_water_, index, day);

  // Stock are reported from the farm's own record of where they were, not from
  // anything drawn: the markers are an illustration of a set stocked mob and
  // the paddock list is what the model actually moved.
  const bool grazed = day < grazed_each_day_.size() &&
                      std::find(grazed_each_day_[day].begin(), grazed_each_day_[day].end(),
                                index) != grazed_each_day_[day].end();

  paddock_label_->setText(
      QString(
          "<b>%1</b> &nbsp; %2 ha, %3 cells &nbsp;|&nbsp; cover %4 &nbsp;|&nbsp; grew %5 "
          "&nbsp;|&nbsp; water left %6 &nbsp;|&nbsp; stress %7 &nbsp;|&nbsp; irrigation %8 today, "
          "%9 so far &nbsp;|&nbsp; %10")
          .arg(QString::fromStdString(paddocks_[index].name.empty()
                                          ? "Paddock " + std::to_string(index + 1)
                                          : paddocks_[index].name))
          .arg(mask_->rasterised_hectares(index), 0, 'f', 1)
          .arg(mask_->cell_counts()[index])
          .arg(figure(paddock_mean(cover_, index, day), 0, " kg DM/ha"))
          .arg(figure(paddock_mean(growth_, index, day), 1, " kg DM/ha today"))
          .arg(left.has_value() ? QString("%1%").arg(*left * 100.0, 0, 'f', 0) : QString("-"))
          .arg(figure(paddock_mean(water_stress_, index, day), 2, ""))
          .arg(figure(paddock_mean(irrigation_today_, index, day), 1, " mm"))
          .arg(figure(paddock_mean(irrigation_to_date_, index, day), 0, " mm"))
          .arg(grazed ? "stock on it" : "no stock"));
}

void MapWindow::show_day(int day) {
  current_day_ = day;
  refresh();
}

void MapWindow::show_weather(std::size_t day, double clearness) {
  if (day >= weather_.size()) {
    weather_line_ = "this run kept no daily weather to show";
    weather_label_->setText("<b>Weather</b> &nbsp; this run kept no daily weather to show");
    return;
  }
  const core::DailyWeather& today = weather_[day];

  const auto describe = [](core::SkyCondition condition) -> const char* {
    switch (condition) {
      case core::SkyCondition::Clear:
        return "clear";
      case core::SkyCondition::PartlyCloudy:
        return "partly cloudy";
      case core::SkyCondition::Overcast:
        return "overcast";
    }
    return "clear";
  };
  const char* sky = describe(core::sky_from_clearness(clearness));

  // Rain is the day's total. There is no intensity and no time of day in the
  // series, so "wet" is as much as can be said about when.
  // Beside the rain, because the two are the same quantity arriving two ways
  // and the point of showing them together is to let somebody weigh one
  // against the other.
  const double watered_mm = day < irrigation_mm_.size() ? irrigation_mm_[day] : 0.0;
  const QString irrigation =
      watered_mm >= 0.05 ? QString("%1 mm").arg(watered_mm, 0, 'f', 1) : QString("none");

  const QString rain = today.rainfall_mm >= 0.05
                           ? QString("%1 mm").arg(today.rainfall_mm, 0, 'f', 1)
                           : QString("dry");

  const core::SunPosition sun =
      core::sun_position(latitude_degrees_, today.date.day_of_year(), kSolarHourShown);

  weather_label_->setText(
      QString("<b>%1</b> &nbsp;&nbsp; %2 &nbsp;&nbsp; rain <b>%3</b> &nbsp;&nbsp; "
              "irrigation <b>%11</b> &nbsp;&nbsp; "
              "%4 to %5 &deg;C &nbsp;&nbsp; sun %6 MJ/m&sup2; "
              "(%7 of what the sky could give) &nbsp;&nbsp; "
              "sun %8&deg; up, bearing %9&deg; at 14:00 solar time &nbsp;&nbsp; "
              "<span style='color:#888'>wind %10 m/s - recorded, not modelled</span>")
          .arg(QString::fromStdString(today.date.to_iso_string()))
          .arg(sky)
          .arg(rain)
          .arg(today.min_air_temperature_c, 0, 'f', 1)
          .arg(today.max_air_temperature_c, 0, 'f', 1)
          .arg(today.solar_radiation_mj_per_m2, 0, 'f', 1)
          .arg(QString("%1%").arg(clearness * 100.0, 0, 'f', 0))
          .arg(sun.elevation_degrees, 0, 'f', 0)
          .arg(sun.azimuth_degrees, 0, 'f', 0)
          .arg(today.wind_speed_m_per_s, 0, 'f', 1)
          .arg(irrigation));

  weather_line_ = QString(
                      "%1  %2, rain %3, %4 to %5 C, sun %6 MJ/m2 (%7 of the sky's), sun %8 deg up "
                      "bearing %9 deg at 14:00 solar, wind %10 m/s (recorded, not modelled), "
                      "irrigation %11")
                      .arg(QString::fromStdString(today.date.to_iso_string()))
                      .arg(sky)
                      .arg(rain)
                      .arg(today.min_air_temperature_c, 0, 'f', 1)
                      .arg(today.max_air_temperature_c, 0, 'f', 1)
                      .arg(today.solar_radiation_mj_per_m2, 0, 'f', 1)
                      .arg(QString("%1%").arg(clearness * 100.0, 0, 'f', 0))
                      .arg(sun.elevation_degrees, 0, 'f', 0)
                      .arg(sun.azimuth_degrees, 0, 'f', 0)
                      .arg(today.wind_speed_m_per_s, 0, 'f', 1)
                      .arg(irrigation)
                      .toStdString();
}

void MapWindow::change_scale(int mode) {
  scale_mode_ = static_cast<ScaleMode>(scale_box_->itemData(mode).toInt());
  refresh();
}

bool MapWindow::select_field(const std::string& name) {
  const int index = field_box_->findText(QString::fromStdString(name), Qt::MatchFixedString);
  if (index < 0) {
    return false;
  }
  field_box_->setCurrentIndex(index);
  return true;
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
  // At the end of the year, Play starts it again rather than doing nothing.
  if (!dates_.empty() && current_day_ >= static_cast<int>(dates_.size()) - 1) {
    timeline_->setValue(0);
  }
  timer_->start();
  play_button_->setText("Pause");
}

void MapWindow::advance_frame() {
  if (dates_.empty()) {
    return;
  }
  // The year ends. Looping back to July made a run of the farm look like
  // something with no beginning and no end, and made it easy to watch the same
  // spring twice without noticing it was the same spring. Pressing Play again
  // starts the year over.
  const int next = current_day_ + 1;
  if (next >= static_cast<int>(dates_.size())) {
    timer_->stop();
    play_button_->setText("Play");
    return;
  }
  timeline_->setValue(next);
}

void MapWindow::render_once() {
  refresh();
  scene_.reset_camera();
  view_->renderWindow()->Render();
}

}  // namespace paddock::app
