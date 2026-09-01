// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#include "DashboardWidgets.hpp"

#include <QAbstractBarSeries>
#include <QBarCategoryAxis>
#include <QBarSeries>
#include <QBarSet>
#include <QChart>
#include <QChartView>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLegendMarker>
#include <QLineSeries>
#include <QPieSeries>
#include <QPieSlice>
#include <QScatterSeries>
#include <QStackedBarSeries>
#include <QVBoxLayout>
#include <QValueAxis>
#include <algorithm>
#include <cmath>

namespace paddock::app::widgets {

namespace {

/// The panel greys, so a chart is one instrument with the window rather than a
/// picture of one. Repeated from SeasonChart rather than shared, because these
/// are the only two places that need them and a header holding four colours is
/// more indirection than it saves.
const QColor kInk(0xDC, 0xE4, 0xF2);
const QColor kDim(0x8A, 0x98, 0xB4);
const QColor kFaint(0x33, 0x40, 0x5A);
const QColor kAccent(0x4C, 0x9A, 0xFF);

void dress(QChart* chart) {
  chart->setBackgroundVisible(false);
  chart->setPlotAreaBackgroundVisible(false);
  chart->setMargins(QMargins(4, 4, 4, 4));
  chart->setTitleBrush(QBrush(kDim));
  QFont title = chart->titleFont();
  title.setPointSizeF(title.pointSizeF() * 0.95);
  chart->setTitleFont(title);
  chart->legend()->setLabelColor(kDim);
  chart->legend()->setAlignment(Qt::AlignBottom);
}

void dress(QAbstractAxis* axis) {
  axis->setLabelsColor(kDim);
  axis->setTitleBrush(QBrush(kDim));
  axis->setLinePenColor(kFaint);
  axis->setGridLineColor(kFaint);
}

QChartView* framed(QChart* chart, int minimum_height) {
  auto* view = new QChartView(chart);
  view->setRenderHint(QPainter::Antialiasing);
  view->setMinimumHeight(minimum_height);
  view->setStyleSheet("background: transparent;");
  return view;
}

/// A number with as many places as it deserves, as everywhere else on the page.
QString figure(double value, const QString& unit) {
  if (std::abs(value) >= 1000.0) {
    return QString::number(value, 'f', 0);
  }
  if (std::abs(value) >= 100.0 || unit == "head" || unit == "days") {
    return QString::number(value, 'f', 0);
  }
  return QString::number(value, 'f', 1);
}

}  // namespace

QColor standing_colour(config::Standing standing) {
  switch (standing) {
    case config::Standing::Over:
      return {0xE0, 0x5A, 0x4B};
    case config::Standing::Watch:
      return {0xE0, 0xA0, 0x30};
    case config::Standing::Good:
      return {0x3F, 0xB9, 0x60};
    case config::Standing::Unmeasured:
      break;
  }
  return kInk;
}

std::optional<config::Indicator> find_indicator(const config::FarmDashboard& board,
                                                const std::string& name) {
  for (const config::DashboardPanel& panel : board.panels) {
    for (const config::Indicator& indicator : panel.indicators) {
      if (indicator.name == name) {
        return indicator;
      }
    }
  }
  return std::nullopt;
}

QWidget* kpi_tile(const QString& name, const QString& value, const QString& unit,
                  config::Standing standing, const QString& trust, const QString& note) {
  auto* tile = new QFrame;
  tile->setObjectName("kpiTile");
  tile->setToolTip(note);

  auto* caption = new QLabel(name.toUpper());
  caption->setObjectName("kpiCaption");

  auto* big = new QLabel(value);
  big->setObjectName("kpiValue");
  QFont large = big->font();
  large.setPointSizeF(large.pointSizeF() * 2.1);
  large.setBold(true);
  big->setFont(large);
  if (const QColor colour = standing_colour(standing); standing != config::Standing::Unmeasured) {
    big->setStyleSheet(QString("color: %1;").arg(colour.name()));
  }

  auto* units = new QLabel(unit);
  units->setObjectName("kpiUnit");

  auto* value_row = new QHBoxLayout;
  value_row->setContentsMargins(0, 0, 0, 0);
  value_row->setSpacing(6);
  value_row->addWidget(big);
  value_row->addWidget(units, 0, Qt::AlignBottom);
  value_row->addStretch(1);

  // **The trust goes on the tile.** This is the most quotable thing on the
  // page - the part that ends up in a slide - and a big number with nothing
  // beside it is an invitation to quote a model output as a measurement.
  auto* provenance = new QLabel(trust);
  provenance->setObjectName("kpiTrust");

  auto* column = new QVBoxLayout;
  column->setContentsMargins(12, 10, 12, 10);
  column->setSpacing(2);
  column->addWidget(caption);
  column->addLayout(value_row);
  column->addWidget(provenance);
  tile->setLayout(column);
  return tile;
}

QWidget* kpi_tile(const config::Indicator& indicator) {
  return kpi_tile(QString::fromStdString(indicator.name),
                  figure(indicator.value, QString::fromStdString(indicator.unit)),
                  QString::fromStdString(indicator.unit), indicator.standing,
                  QString::fromStdString(config::to_string(indicator.trust)),
                  QString::fromStdString(indicator.note));
}

QWidget* bars_by_year(const std::vector<config::FarmDashboard>& boards,
                      const std::string& indicator, const QString& title,
                      const std::vector<Reference>& references, bool colour_standing) {
  auto* chart = new QChart;
  chart->setTitle(title);
  dress(chart);

  QStringList years;
  std::vector<std::optional<config::Indicator>> found;
  found.reserve(boards.size());
  double top = 0.0;
  QString unit;
  bool any = false;
  for (const config::FarmDashboard& board : boards) {
    // **The opening year alone.** "2015-16" laid sideways was clipped to
    // "2015..." on every bar; a farm year is named by the July it opens in, and
    // four characters sit flat under ten bars where seven do not.
    years << QString::fromStdString(board.label).left(4);
    std::optional<config::Indicator> one = find_indicator(board, indicator);
    if (one.has_value()) {
      any = true;
      unit = QString::fromStdString(one->unit);
      top = std::max(top, one->value);
    }
    found.push_back(std::move(one));
  }
  if (!any) {
    delete chart;
    return new QWidget;
  }

  // **A bar per standing, not a bar per year.** Qt colours a QBarSet, never an
  // individual bar in one, so colouring by standing means one set per standing
  // with a value at the years that have it and zero everywhere else, stacked so
  // that each year's single non-zero bar occupies its own slot.
  //
  // This is the second attempt. The first set a colour on the one set and
  // called setBarSelected() per index, which does nothing to a bar's colour -
  // so `colour_standing` was a parameter that was passed, stored, branched on,
  // and changed nothing at all on screen.
  QAbstractBarSeries* series = nullptr;
  if (colour_standing) {
    auto* stacked = new QStackedBarSeries;

    struct Band {
      config::Standing standing;
      const char* name;
    };

    for (const Band& band : {Band{config::Standing::Good, "Inside the band"},
                             Band{config::Standing::Watch, "Near the edge"},
                             Band{config::Standing::Over, "Outside it"},
                             Band{config::Standing::Unmeasured, "Nothing to measure against"}}) {
      auto* set = new QBarSet(band.name);
      set->setColor(band.standing == config::Standing::Unmeasured ? kAccent
                                                                  : standing_colour(band.standing));
      set->setBorderColor(Qt::transparent);
      bool used = false;
      for (const std::optional<config::Indicator>& one : found) {
        const bool mine = one.has_value() && one->standing == band.standing;
        *set << (mine ? one->value : 0.0);
        used = used || mine;
      }
      if (used) {
        stacked->append(set);
      } else {
        delete set;
      }
    }
    series = stacked;
  } else {
    auto* plain = new QBarSeries;
    auto* set = new QBarSet(QString::fromStdString(indicator));
    set->setColor(kAccent);
    set->setBorderColor(Qt::transparent);
    for (const std::optional<config::Indicator>& one : found) {
      *set << (one.has_value() ? one->value : 0.0);
    }
    plain->append(set);
    series = plain;
  }
  series->setLabelsVisible(false);
  chart->addSeries(series);
  chart->legend()->setVisible(false);

  auto* axis_x = new QBarCategoryAxis;
  axis_x->append(years);
  axis_x->setLabelsAngle(0);
  dress(axis_x);
  chart->addAxis(axis_x, Qt::AlignBottom);
  series->attachAxis(axis_x);

  auto* axis_y = new QValueAxis;
  axis_y->setTitleText(unit);
  axis_y->setLabelFormat("%.0f");
  axis_y->setTickCount(6);
  dress(axis_y);
  chart->addAxis(axis_y, Qt::AlignLeft);
  series->attachAxis(axis_y);

  // **References are drawn, not described.** A measured mean quoted in a
  // caption is a number the reader has to hold in their head while looking at
  // ten bars; drawn across them it is the thing they are comparing against.
  for (const Reference& reference : references) {
    auto* level = new QLineSeries;
    level->setName(reference.name);
    level->setPen(QPen(reference.colour, 1.2, Qt::DashLine));
    level->append(-0.5, reference.value);
    level->append(static_cast<double>(boards.size()) - 0.5, reference.value);
    chart->addSeries(level);

    // A line series cannot share a category axis, so it gets a hidden value
    // axis over the same span and the same vertical scale.
    auto* hidden_x = new QValueAxis;
    hidden_x->setRange(-0.5, static_cast<double>(boards.size()) - 0.5);
    hidden_x->setVisible(false);
    chart->addAxis(hidden_x, Qt::AlignBottom);
    level->attachAxis(hidden_x);
    level->attachAxis(axis_y);

    top = std::max(top, reference.value);
    chart->legend()->setVisible(true);
    for (QLegendMarker* marker : chart->legend()->markers(series)) {
      marker->setVisible(false);
    }
  }

  axis_y->setRange(0.0, top > 0.0 ? top * 1.12 : 1.0);
  return framed(chart, 240);
}

QWidget* donut(const QString& title, const QString& centre, const std::vector<Slice>& slices) {
  auto* chart = new QChart;
  chart->setTitle(title);
  dress(chart);

  auto* pie = new QPieSeries;
  pie->setHoleSize(0.45);
  double total = 0.0;
  for (const Slice& slice : slices) {
    total += std::max(0.0, slice.value);
  }
  for (const Slice& slice : slices) {
    if (slice.value <= 0.0) {
      continue;
    }
    QPieSlice* wedge = pie->append(slice.name, slice.value);
    wedge->setColor(slice.colour);
    wedge->setBorderColor(QColor(0x14, 0x1A, 0x26));
    wedge->setBorderWidth(2);
    wedge->setLabelColor(kDim);
    // **The name and the share travel together, in the legend.** Qt takes the
    // legend's text from the slice's label, so writing a bare percentage there
    // - which is what this did first - produced a key reading "13%" and "87%"
    // with nothing to say which was which. Written inside the ring instead it
    // would be unreadable on the thin slices, which are the ones worth reading.
    const double share = total > 0.0 ? slice.value / total * 100.0 : 0.0;
    wedge->setLabel(QString("%1  %2%").arg(slice.name).arg(share, 0, 'f', 0));
    wedge->setLabelVisible(false);
  }
  if (pie->count() == 0) {
    delete chart;
    delete pie;
    return new QWidget;
  }
  chart->addSeries(pie);
  chart->legend()->setVisible(true);
  chart->legend()->setAlignment(Qt::AlignRight);

  // **The whole goes in the title, not in the hole.** A label parented to the
  // view with no geometry set sits at its top-left corner, which is where the
  // total was appearing - a number in the corner of a ring reads as a stray
  // figure rather than as what the ring adds up to. A ring of shares still
  // needs its total said somewhere: the title is somewhere.
  if (!centre.isEmpty()) {
    chart->setTitle(title + "   -   " + centre);
  }
  return framed(chart, 240);
}

QWidget* scatter_by_year(const std::vector<config::FarmDashboard>& boards,
                         const std::string& x_indicator, const std::string& y_indicator,
                         const QString& title) {
  auto* chart = new QChart;
  chart->setTitle(title);
  dress(chart);

  auto* points = new QScatterSeries;
  points->setMarkerSize(11.0);
  points->setColor(kAccent);
  points->setBorderColor(QColor(0x14, 0x1A, 0x26));

  QString x_unit;
  QString y_unit;
  double x_top = 0.0;
  double y_top = 0.0;
  double x_low = 0.0;
  double y_low = 0.0;
  bool first = true;
  for (const config::FarmDashboard& board : boards) {
    const std::optional<config::Indicator> x = find_indicator(board, x_indicator);
    const std::optional<config::Indicator> y = find_indicator(board, y_indicator);
    if (!x.has_value() || !y.has_value()) {
      continue;
    }
    points->append(x->value, y->value);
    x_unit = QString::fromStdString(x->unit);
    y_unit = QString::fromStdString(y->unit);
    x_top = first ? x->value : std::max(x_top, x->value);
    y_top = first ? y->value : std::max(y_top, y->value);
    x_low = first ? x->value : std::min(x_low, x->value);
    y_low = first ? y->value : std::min(y_low, y->value);
    first = false;
  }
  if (first) {
    delete chart;
    delete points;
    return new QWidget;
  }

  chart->addSeries(points);
  chart->legend()->setVisible(false);

  auto* axis_x = new QValueAxis;
  axis_x->setTitleText(QString::fromStdString(x_indicator) + ", " + x_unit);
  axis_x->setLabelFormat("%.0f");
  axis_x->setTickCount(5);
  // Padded rather than tight: a point on the frame is a point half drawn.
  const double x_pad = std::max(1.0, (x_top - x_low) * 0.12);
  axis_x->setRange(x_low - x_pad, x_top + x_pad);
  dress(axis_x);
  chart->addAxis(axis_x, Qt::AlignBottom);
  points->attachAxis(axis_x);

  auto* axis_y = new QValueAxis;
  // The unit alone: a title naming the indicator as well was clipped to
  // "Nitrate leached (kg N/..." in the space a side axis has, and the chart's
  // own title already says which indicator this is.
  axis_y->setTitleText(y_unit);
  axis_y->setLabelFormat("%.0f");
  axis_y->setTickCount(5);
  const double y_pad = std::max(1.0, (y_top - y_low) * 0.12);
  axis_y->setRange(std::max(0.0, y_low - y_pad), y_top + y_pad);
  dress(axis_y);
  chart->addAxis(axis_y, Qt::AlignLeft);
  points->attachAxis(axis_y);

  return framed(chart, 240);
}

QWidget* card(const QString& title, QWidget* body) {
  auto* frame = new QFrame;
  frame->setObjectName("panelCard");

  auto* column = new QVBoxLayout;
  column->setContentsMargins(8, 8, 8, 8);
  column->setSpacing(4);
  if (!title.isEmpty()) {
    auto* heading = new QLabel(title);
    heading->setObjectName("cardTitle");
    column->addWidget(heading);
  }
  column->addWidget(body, 1);
  frame->setLayout(column);
  return frame;
}

}  // namespace paddock::app::widgets
