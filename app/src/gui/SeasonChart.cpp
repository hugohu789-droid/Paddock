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
                           const std::vector<Events>& events, Scale scale) {
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
  chart_->setTitle("The farm through the year   -   marks below are the days it was irrigated");
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
  // Every month, with the names turned upright. Seven ticks across a year
  // meant the axis named five of the twelve and left the reader counting
  // between them; turned on their side, all thirteen boundaries fit.
  time_axis_->setTickCount(13);
  time_axis_->setLabelsAngle(-90);
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
  // **Each axis is named after the one quantity on it.** At most two lines are
  // ever drawn, so an axis never has to stand for more than one thing - it can
  // carry the name, the unit and the range of that thing alone.
  // **The name alone on the axis; the unit goes in the key.** An axis title is
  // written down the side, and "Pasture cover (kg DM/ha)" is longer than a
  // chart this height can show - it came out elided, which is a label that
  // stops halfway through a word. The axis is drawn in its line's own colour
  // and the key beside it gives the unit, so nothing is lost by shortening it.
  const auto describe = [](const Line& line) { return line.name; };
  left_axis_->setTitleText(lines.empty()            ? QString()
                           : scale == Scale::Shared ? lines.front().unit
                                                    : describe(lines.front()));
  dress(left_axis_);
  chart_->addAxis(left_axis_, Qt::AlignLeft);
  right_axis_ = new QValueAxis(chart_);
  right_axis_->setTitleText(scale == Scale::PerLine && lines.size() > 1 ? describe(lines[1])
                                                                        : QString());
  dress(right_axis_);
  chart_->addAxis(right_axis_, Qt::AlignRight);

  // First chosen on the left, second on the right. Position rather than a fixed
  // side, so any two quantities can be put beside each other - the pair
  // somebody wants to compare is not something this can know in advance.
  // On a shared scale the range has to cover every line before any of them is
  // drawn, because they all read off the one axis. Worked out first for that
  // reason; PerLine sets its own inside the loop from each line alone.
  double shared_top = 0.0;
  if (scale == Scale::Shared) {
    for (const Line& line : lines) {
      const auto highest = std::max_element(line.values.begin(), line.values.end());
      if (highest != line.values.end()) {
        shared_top = std::max(shared_top, *highest);
      }
      if (line.reference.has_value()) {
        shared_top = std::max(shared_top, *line.reference);
      }
    }
  }

  const std::size_t drawn =
      scale == Scale::Shared ? lines.size() : std::min<std::size_t>(2, lines.size());
  for (std::size_t i = 0; i < drawn; ++i) {
    const Line& line = lines[i];
    QValueAxis* axis = (scale == Scale::Shared || i == 0) ? left_axis_ : right_axis_;

    auto* series = new QLineSeries(chart_);
    series->setName(line.unit.isEmpty() ? line.name
                                        : QString("%1 (%2)").arg(line.name).arg(line.unit));
    series->setPen(QPen(line.colour, 1.6));
    for (std::size_t day = 0; day < line.values.size() && day < dates_.size(); ++day) {
      series->append(static_cast<double>(stamp(dates_[day])), line.values[day]);
    }
    chart_->addSeries(series);
    series->attachAxis(time_axis_);
    series->attachAxis(axis);

    // **From zero to this line's own highest.** Zero at the bottom rather than
    // its lowest value: an axis that begins at 1,900 makes a farm that dropped
    // a fifth of its cover look like one that dropped to nothing. Every
    // quantity here is one that cannot go below zero, so nothing is cut off.
    const auto highest = std::max_element(line.values.begin(), line.values.end());
    double top = highest == line.values.end() ? 1.0 : *highest;

    // **The level this line is read against**, dashed and in the same colour so
    // it belongs to the line rather than floating over the chart. Kept out of
    // the legend: it is the same quantity, not another one.
    if (line.reference.has_value() && !dates_.empty()) {
      auto* level = new QLineSeries(chart_);
      QPen pen(line.colour, 1.0, Qt::DashLine);
      level->setPen(pen);
      level->append(static_cast<double>(stamp(dates_.front())), *line.reference);
      level->append(static_cast<double>(stamp(dates_.back())), *line.reference);
      chart_->addSeries(level);
      level->attachAxis(time_axis_);
      level->attachAxis(axis);
      chart_->legend()->markers(level).front()->setVisible(false);

      // The axis has to reach it, or a farm sitting under its own floor draws a
      // line the chart has cropped off the top.
      top = std::max(top, *line.reference);
    }

    if (scale == Scale::Shared) {
      // **One range, and the axis stays the panel's own grey.** Coloured to any
      // one year it would claim that year owns the scale, when the whole point
      // is that every line is read off it.
      axis->setRange(0.0, shared_top > 0.0 ? shared_top * 1.05 : 1.0);
      axis->setLabelFormat(shared_top < 10.0 ? "%.2f" : "%.0f");
    } else {
      axis->setRange(0.0, top > 0.0 ? top * 1.05 : 1.0);
      axis->setLabelFormat(top < 10.0 ? "%.2f" : "%.0f");
      axis->setLabelsColor(line.colour);
      axis->setTitleBrush(QBrush(line.colour));
    }
  }

  // An axis with nothing on it is not drawn: an empty scale beside a chart is a
  // reader looking for the line that belongs to it.
  right_axis_->setVisible(scale == Scale::PerLine && lines.size() > 1);

  // The left axis starts at zero rather than at the lowest cover of the year.
  // A cover axis that begins at 1,900 makes a farm that dropped a fifth look
  // like one that dropped to nothing - the difference is real either way, and
  // the picture should not exaggerate it.

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
  // **Lines and marks get different glyphs, and are kept apart.**
  //
  // Listed alike, two lines and one row of marks read as three lines - and the
  // first person shown it counted three quantities where two had been chosen.
  // A dash is a line and a dot is an event, which is what they look like on the
  // chart, and the word "on" separates what is plotted from what merely
  // happened.
  const auto entry = [](const QColor& colour, const QString& glyph, const QString& name) {
    return QString("<span style='color:%1'>%2</span>&nbsp;%3")
        .arg(colour.name())
        .arg(glyph)
        .arg(name.toHtmlEscaped());
  };

  QString plotted;
  for (const Line& line : lines) {
    // With the unit, because on a per-line scale the axis no longer carries it.
    //
    // **Except on a shared scale, where it does.** Ten years of cover put
    // "(kg DM/ha)" after every one of ten names and wrapped the key onto three
    // rows to say the same three words ten times - while the one axis they all
    // read off had it written once already.
    const QString named = (line.unit.isEmpty() || scale == Scale::Shared)
                              ? line.name
                              : QString("%1 (%2)").arg(line.name).arg(line.unit);
    plotted += (plotted.isEmpty() ? "" : "&nbsp;&nbsp;&nbsp;") +
               entry(line.colour, "&#9473;&#9473;", named);
  }

  QString happened;
  for (const Events& marks : events) {
    happened += (happened.isEmpty() ? "" : "&nbsp;&nbsp;&nbsp;") +
                entry(marks.colour, "&#9679;", marks.name);
  }

  key_ = plotted;
  if (!happened.isEmpty()) {
    key_ += (key_.isEmpty() ? ""
                            : "&nbsp;&nbsp;&nbsp;&nbsp;<span style='color:#8A98B4'>|</span>"
                              "&nbsp;&nbsp;&nbsp;&nbsp;") +
            QString("<span style='color:#8A98B4'>days:</span>&nbsp;") + happened;
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
