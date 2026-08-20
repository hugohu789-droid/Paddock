// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#pragma once

#include <QChart>
#include <QChartView>
#include <QColor>
#include <QDateTimeAxis>
#include <QLineSeries>
#include <QString>
#include <QValueAxis>
#include <QWidget>
#include <vector>

namespace paddock::app {

/// A year of the farm, plotted against time.
///
/// **A map answers "where" and this answers "when".** The scene beside it shows
/// one day over the whole farm; this shows the whole run at one place on it, and
/// the two questions a farm adviser asks - which paddock, and which month - need
/// both.
///
/// **Two real axes, not one shared one.** Pasture cover is thousands of
/// kilograms and soil moisture is a fraction; drawn against a single scale one
/// of them is a flat line along an edge, and drawn against a normalised scale
/// neither can be read off at all. Cover takes the left axis in its own units
/// and moisture the right in its own, which is what a chart with two quantities
/// in it is supposed to do.
class SeasonChart : public QChartView {
  Q_OBJECT

 public:
  /// A quantity that has a value every day.
  struct Line {
    QString name;
    QString unit;
    QColor colour;
    std::vector<double> values;
    /// Which axis it belongs to. The left carries whatever is measured in the
    /// farm's own working units; the right carries fractions.
    bool on_right_axis = false;
  };

  /// Something that either happened on a day or did not.
  ///
  /// Kept apart from the lines on purpose. An irrigation is an event, not a
  /// level: drawn as a line it would slope between the days it happened on,
  /// which is the picture inventing water on days that had none.
  struct Events {
    QString name;
    QColor colour;
    std::vector<bool> days;
  };

  explicit SeasonChart(QWidget* parent = nullptr);

  /// `dates` are ISO strings, one per day, and become the time axis.
  void show_run(const std::vector<QString>& dates, const std::vector<Line>& lines,
                const std::vector<Events>& events);

  /// Marks the day the rest of the window is showing, so the chart and the map
  /// cannot disagree about which day is being looked at.
  void mark_day(int day);

  void clear();

 signals:
  /// A day was clicked. The window moves the timeline to it, which is what
  /// makes the chart a way of getting somewhere rather than only of looking.
  void dayPicked(int day);

 protected:
  void mousePressEvent(QMouseEvent* event) override;

 private:
  std::vector<QString> dates_;
  int marked_ = 0;

  QChart* chart_ = nullptr;
  QDateTimeAxis* time_axis_ = nullptr;
  QValueAxis* left_axis_ = nullptr;
  QValueAxis* right_axis_ = nullptr;
  /// Carries the event rows and is never drawn - see the note where it is
  /// built.
  QValueAxis* event_axis_ = nullptr;
  /// The line marking the day on screen, kept so it can be moved rather than
  /// rebuilt with the rest of the chart every time the timeline ticks.
  QLineSeries* marker_ = nullptr;

  void place_marker();
};

}  // namespace paddock::app
