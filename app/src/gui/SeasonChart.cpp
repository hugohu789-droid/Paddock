// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#include "SeasonChart.hpp"

#include <QBrush>
#include <QColor>
#include <QDateTime>
#include <QLegend>
#include <QLegendMarker>
#include <QLineSeries>
#include <QLocale>
#include <QMouseEvent>
#include <QPen>
#include <QScatterSeries>
#include <algorithm>
#include <cstddef>
#include <limits>

namespace paddock::app {

namespace {

/// Where the event rows sit on their own hidden axis, which runs 0 to 1.
///
/// Events have no height - a day either was irrigated or was not - so each gets
/// a row near the foot of the plot, below where the lines usually reach.
constexpr double kEventRowSpacing = 0.05;
constexpr double kEventRowTop = 0.06;

/// Turns an ISO date into the milliseconds QDateTimeAxis counts in.
qint64 stamp(const QString& iso) {
  return QDateTime(QDate::fromString(iso, Qt::ISODate), QTime(12, 0)).toMSecsSinceEpoch();
}

/// **English months, whatever the machine is set to.**
///
/// QDateTimeAxis formats through the system locale, so on a Chinese Windows the
/// axis came out reading 7 yue, 8 yue - in a window whose every other word is
/// English, and in a project whose rule is that everything in the repository is
/// written in English. A chart is part of the repository the moment somebody
/// puts it in a report.
QLocale english_dates() {
  return {QLocale::English, QLocale::NewZealand};
}

}  // namespace

SeasonChart::SeasonChart(QWidget* parent) : QChartView(parent) {
  chart_ = new QChart;
  // **No legend; the names are on the axes and in the title.**
  //
  // The legend was tried and reserved a column of empty space beside the plot
  // without ever drawing into it, at every alignment and with the label colour
  // set by hand. Rather than keep guessing at it, the two lines are named by
  // the axes they read off - which a legend cannot say anyway - and the two
  // rows of marks are named in the title. That is deterministic, and it costs
  // no width.
  chart_->legend()->setVisible(false);
  // **Margins wide enough for the axes, which are drawn in them.**
  //
  // Squeezed to two pixels to save room, and the chart quietly stopped drawing
  // its axis labels, its axis titles and its title - all of which live outside
  // the plot area and inside the margin. The symptom was a chart with no
  // numbers on it, and the cause was not the axes at all.
  // **The same dark as the window around it.** A white chart beside a dark
  // instrument panel reads as a document somebody pasted in, and next to a
  // night-dark scene it is the brightest thing on screen - which is the wrong
  // thing to draw the eye, since the map is where the farm is.
  chart_->setBackgroundBrush(QBrush(QColor(0x1B, 0x22, 0x30)));
  chart_->setBackgroundRoundness(8.0);
  // The plot area is left transparent. Given its own dark brush it covered the
  // axes, which is a chart with no numbers on it - the background was worth
  // less than they are.
  chart_->setTitleBrush(QBrush(QColor(0x8A, 0x98, 0xB4)));
  chart_->setLocale(english_dates());
  chart_->setLocalizeNumbers(false);
  setChart(chart_);
  setRenderHint(QPainter::Antialiasing, true);
  setMinimumWidth(280);
  setCursor(Qt::CrossCursor);
  setToolTip(
      "The whole run, day by day.\n\nPasture cover reads off the left axis and soil moisture off "
      "the right; the marks below are days something happened. Click to go to a day.");
  clear();
}

void SeasonChart::clear() {
  chart_->removeAllSeries();
  dates_.clear();
  marker_ = nullptr;
  chart_->setTitle("Run a scenario to see the year");
}

void SeasonChart::show_run(const std::vector<QString>& dates, const std::vector<Line>& lines,
                           const std::vector<Events>& events) {
  // **The old axes are deleted, not merely detached.**
  //
  // They are built with the chart as their parent, so removeAxis takes them off
  // the chart and leaves them alive as its children. Every run added another
  // set; after the first, the chart was carrying four, then six, and stopped
  // drawing any of them or honouring a new title. The symptom was a chart with
  // no numbers on it, which is not a chart.
  const QList<QAbstractAxis*> stale = chart_->axes();
  chart_->removeAllSeries();
  for (QAbstractAxis* axis : stale) {
    chart_->removeAxis(axis);
    delete axis;
  }
  time_axis_ = nullptr;
  left_axis_ = nullptr;
  right_axis_ = nullptr;
  event_axis_ = nullptr;
  marker_ = nullptr;
  dates_ = dates;
  if (dates_.empty()) {
    clear();
    return;
  }
  chart_->setTitle(
      "The farm through the year   -   marks below: irrigation, then days the mob moved");
  marked_ = std::clamp(marked_, 0, static_cast<int>(dates_.size()) - 1);

  // Axes in the panel's own greys, so the chart is one instrument with the
  // window rather than a picture of one.
  const QColor ink(0xDC, 0xE4, 0xF2);
  const QColor faint(0x33, 0x40, 0x5A);
  const auto dress = [&ink, &faint](QAbstractAxis* axis) {
    axis->setLabelsColor(ink);
    axis->setTitleBrush(QBrush(ink));
    axis->setLinePenColor(faint);
    axis->setGridLineColor(faint);
  };

  time_axis_ = new QDateTimeAxis(chart_);
  time_axis_->setFormat("MMM");
  time_axis_->setTickCount(7);
  time_axis_->setTitleText("Month");
  dress(time_axis_);
  chart_->addAxis(time_axis_, Qt::AlignBottom);

  // **Named twice over: on the axis and in the legend.**
  //
  // The legend says which colour is which quantity, including the two rows of
  // event marks that have no axis of their own. The axis titles say which side
  // each line is read off, which the legend cannot - and they survive a chart
  // too short for a legend, which this one can be when the strip under the map
  // is dragged small.
  left_axis_ = new QValueAxis(chart_);
  left_axis_->setTitleText(lines.empty() ? QString() : lines.front().name);
  dress(left_axis_);
  chart_->addAxis(left_axis_, Qt::AlignLeft);
  right_axis_ = new QValueAxis(chart_);
  const auto on_right =
      std::find_if(lines.begin(), lines.end(), [](const Line& line) { return line.on_right_axis; });
  right_axis_->setTitleText(on_right == lines.end() ? QString() : on_right->name);
  dress(right_axis_);
  chart_->addAxis(right_axis_, Qt::AlignRight);

  double left_highest = 0.0;
  for (const Line& line : lines) {
    auto* series = new QLineSeries(chart_);
    series->setName(line.unit.isEmpty() ? line.name
                                        : QString("%1 (%2)").arg(line.name).arg(line.unit));
    series->setPen(QPen(line.colour, 1.6));
    for (std::size_t day = 0; day < line.values.size() && day < dates_.size(); ++day) {
      series->append(static_cast<double>(stamp(dates_[day])), line.values[day]);
    }
    chart_->addSeries(series);
    series->attachAxis(time_axis_);
    series->attachAxis(line.on_right_axis ? right_axis_ : left_axis_);
    if (!line.on_right_axis) {
      const auto highest = std::max_element(line.values.begin(), line.values.end());
      if (highest != line.values.end()) {
        left_highest = std::max(left_highest, *highest);
      }
    }
  }

  // The left axis starts at zero rather than at the lowest cover of the year.
  // A cover axis that begins at 1,900 makes a farm that dropped a fifth look
  // like one that dropped to nothing - the difference is real either way, and
  // the picture should not exaggerate it.
  left_axis_->setRange(0.0, left_highest > 0.0 ? left_highest * 1.05 : 1.0);
  right_axis_->setRange(0.0, 1.0);
  right_axis_->setLabelFormat("%.1f");

  // **The event rows get an axis of their own, and it is not drawn.**
  //
  // Hung off the left axis they dragged its bottom below zero, and the chart
  // then offered a reader ticks at minus eight hundred kilograms of dry matter
  // per hectare - a quantity that cannot exist, on the axis they are supposed
  // to read the cover from. A hidden axis puts the marks where they belong
  // without saying anything false about the scale beside them.
  event_axis_ = new QValueAxis(chart_);
  event_axis_->setRange(0.0, 1.0);
  event_axis_->setVisible(false);
  chart_->addAxis(event_axis_, Qt::AlignLeft);

  double row = kEventRowTop;
  for (const Events& marks : events) {
    auto* series = new QScatterSeries(chart_);
    series->setName(marks.name);
    series->setColor(marks.colour);
    series->setBorderColor(marks.colour);
    series->setMarkerSize(5.0);
    for (std::size_t day = 0; day < marks.days.size() && day < dates_.size(); ++day) {
      if (marks.days[day]) {
        series->append(static_cast<double>(stamp(dates_[day])), row);
      }
    }
    chart_->addSeries(series);
    series->attachAxis(time_axis_);
    series->attachAxis(event_axis_);
    row -= kEventRowSpacing;
  }

  // The key, from the same colours the series were just given.
  key_.clear();
  const auto swatch = [](const QColor& colour, const QString& name) {
    return QString("<span style='color:%1'>&#9632;</span>&nbsp;%2")
        .arg(colour.name())
        .arg(name.toHtmlEscaped());
  };
  for (const Line& line : lines) {
    key_ += (key_.isEmpty() ? "" : "&nbsp;&nbsp;&nbsp;") + swatch(line.colour, line.name);
  }
  for (const Events& marks : events) {
    key_ += (key_.isEmpty() ? "" : "&nbsp;&nbsp;&nbsp;") + swatch(marks.colour, marks.name);
  }
  emit keyChanged(key_);

  place_marker();
}

QString SeasonChart::colour_key() const {
  return key_;
}

void SeasonChart::place_marker() {
  if (dates_.empty() || left_axis_ == nullptr) {
    return;
  }
  if (marker_ == nullptr) {
    marker_ = new QLineSeries(chart_);
    marker_->setName("Day shown");
    marker_->setPen(QPen(palette().color(QPalette::Highlight), 1.4));
    chart_->addSeries(marker_);
    marker_->attachAxis(time_axis_);
    marker_->attachAxis(left_axis_);
    // The marker is a line, not a quantity, so it does not belong in the
    // legend. Guarded because markers() returns a list that can be empty, and
    // front() on an empty one is undefined - a crash that would only show up
    // on whichever Qt build happened to return nothing.
    const QList<QLegendMarker*> entries = chart_->legend()->markers(marker_);
    if (!entries.isEmpty()) {
      entries.front()->setVisible(false);
    }
  }
  const auto at = static_cast<double>(stamp(dates_[static_cast<std::size_t>(marked_)]));
  marker_->clear();
  marker_->append(at, left_axis_->min());
  marker_->append(at, left_axis_->max());
}

void SeasonChart::mark_day(int day) {
  if (dates_.empty()) {
    return;
  }
  const int clamped = std::clamp(day, 0, static_cast<int>(dates_.size()) - 1);
  if (clamped == marked_) {
    return;
  }
  marked_ = clamped;
  place_marker();
}

void SeasonChart::mousePressEvent(QMouseEvent* event) {
  if (!dates_.empty() && chart_ != nullptr && time_axis_ != nullptr) {
    // Back through the chart's own mapping rather than through arithmetic on
    // the widget's width: the plot area is inset by the axes and their labels,
    // and a click read against the whole widget lands days out at the edges.
    const QPointF value = chart_->mapToValue(event->position(), chart_->series().front());
    const auto wanted = static_cast<qint64>(value.x());
    // The nearest day, because a click lands between two of them.
    std::size_t nearest = 0;
    qint64 closest = std::numeric_limits<qint64>::max();
    for (std::size_t day = 0; day < dates_.size(); ++day) {
      const qint64 away = std::abs(stamp(dates_[day]) - wanted);
      if (away < closest) {
        closest = away;
        nearest = day;
      }
    }
    emit dayPicked(static_cast<int>(nearest));
  }
  QChartView::mousePressEvent(event);
}

}  // namespace paddock::app
