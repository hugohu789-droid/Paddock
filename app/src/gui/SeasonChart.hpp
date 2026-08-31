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
#include <optional>
#include <vector>

namespace paddock::app {

/// A year of the farm, plotted against time.
///
/// **A map answers "where" and this answers "when".** The scene beside it shows
/// one day over the whole farm; this shows the whole run at one place on it, and
/// the two questions a farm adviser asks - which paddock, and which month - need
/// both.
///
/// **Two lines at most, and each owns an axis.** Pasture cover is thousands of
/// kilograms and soil moisture is a fraction; drawn against a single scale one
/// of them is a flat line along an edge, and drawn against a normalised scale
/// neither can be read off at all.
///
/// Because there are never more than two, each axis belongs to one quantity
/// outright: it carries that quantity's name, its unit and its own range. An
/// axis shared by three would have to be titled by unit and scaled to whichever
/// of them ran highest, and a reader would be back to matching lines to axes by
/// colour.
class SeasonChart : public QChartView {
  Q_OBJECT

 public:
  /// A quantity that has a value every day.
  struct Line {
    QString name;
    QString unit;
    QColor colour;
    std::vector<double> values;

    /// A level drawn across the chart, dashed, in this line's own colour - the
    /// cover a farmer holds the farm to, a regulatory trigger.
    ///
    /// **A series without one is read against nothing.** A cover line falling
    /// from 2,800 to 1,400 looks like a farm in trouble or a farm doing exactly
    /// what it planned, and which of those it is depends on a number that was
    /// nowhere on the chart until this existed.
    std::optional<double> reference;
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

  /// A line of coloured names for whatever is drawn, as rich text.
  ///
  /// **Which colour is which is the one thing a chart cannot leave unsaid.**
  /// The axis titles say which side each line reads off, but not which of the
  /// two lines is which colour, and the rows of marks have no axis to be named
  /// by at all. Built here from the same colours the series were given, so a
  /// colour changed in one place cannot disagree with the key in another.
  [[nodiscard]] QString colour_key() const;

  void clear();

 signals:
  /// The colour key changed, because a new run was drawn.
  void keyChanged(const QString& key);

  /// A day was clicked. The window moves the timeline to it, which is what
  /// makes the chart a way of getting somewhere rather than only of looking.
  void dayPicked(int day);

 protected:
  void mousePressEvent(QMouseEvent* event) override;

 private:
  std::vector<QString> dates_;
  QString key_;
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
