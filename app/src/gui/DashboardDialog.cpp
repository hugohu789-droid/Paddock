// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#include "DashboardDialog.hpp"

#include <QBrush>
#include <QColor>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFont>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QTextStream>
#include <QVBoxLayout>
#include <cmath>
#include <utility>

#include "Theme.hpp"

namespace paddock::app {

namespace {

/// A colour per line, from a small fixed set.
///
/// **Fixed rather than generated**, so the same series is the same colour every
/// time the page is opened: a reader who has learned that cover is the blue one
/// should not have to relearn it because a series was added above it.
QColor line_colour(int index) {
  static const QColor kColours[] = {
      QColor(0x1F, 0x77, 0xB4), QColor(0x2C, 0xA0, 0x2C), QColor(0xD6, 0x27, 0x28),
      QColor(0xFF, 0x7F, 0x0E), QColor(0x94, 0x67, 0xBD), QColor(0x8C, 0x56, 0x4B),
  };
  constexpr int kCount = static_cast<int>(sizeof(kColours) / sizeof(kColours[0]));
  return kColours[((index % kCount) + kCount) % kCount];
}

/// A number with as many places as it deserves. Counts get none: "23.00 head"
/// is two ewes' worth of spurious precision on a figure that cannot be
/// fractional.
QString figure(const config::Indicator& indicator) {
  if (indicator.unit == "head" || indicator.unit == "days") {
    return QString::number(indicator.value, 'f', 0);
  }
  return QString::number(indicator.value, 'f', std::abs(indicator.value) >= 100.0 ? 0 : 2);
}

/// The band, as a person would say it out loud.
QString band_of(const config::Indicator& indicator) {
  if (indicator.low.has_value() && indicator.high.has_value()) {
    return QString::number(*indicator.low, 'f', 0) + " to " +
           QString::number(*indicator.high, 'f', 0);
  }
  if (indicator.low.has_value()) {
    return "at least " + QString::number(*indicator.low, 'f', 0);
  }
  if (indicator.high.has_value()) {
    return "up to " + QString::number(*indicator.high, 'f', 0);
  }
  return {};
}

/// **Colour carries the standing, and the word carries it too.** A page that
/// said "outside" only in red would say nothing to a reader who cannot
/// distinguish it, and nothing at all in a printed copy.
QColor colour_for(config::Standing standing) {
  switch (standing) {
    case config::Standing::Over:
      return QColor(0xC0, 0x39, 0x2B);
    case config::Standing::Watch:
      return QColor(0xB9, 0x77, 0x0F);
    case config::Standing::Good:
      return QColor(0x27, 0x7A, 0x3E);
    case config::Standing::Unmeasured:
      break;
  }
  return {};
}

}  // namespace

DashboardDialog::DashboardDialog(config::FarmDashboard dashboard, QWidget* parent)
    : QDialog(parent), dashboard_(std::move(dashboard)) {
  setWindowTitle("Paddock - " + QString::fromStdString(dashboard_.farm) + ", " +
                 QString::fromStdString(dashboard_.label));

  // **The trust line goes at the top, not in a footnote.** It is the number
  // that says how much of the rest means anything, and a reader who stops after
  // the first screen should have seen it.
  auto* trust = new QLabel(
      QString("%1 of %2 indicators rest on something published or on a stated fit. The rest are "
              "placeholders: the right order of magnitude, and not measurements.")
          .arg(dashboard_.indicators_on_evidence())
          .arg(dashboard_.indicators_total()),
      this);
  trust->setWordWrap(true);
  trust->setObjectName("caveat");

  build_table();
  build_chart();

  // **Which colour is which**, which SeasonChart emits and neither dialog was
  // listening for. The main window has carried this label since the chart was
  // written; these two pages showed the same chart with nothing to read it by.
  // Ten years of one quantity make that unmissable - the axis says kg DM/ha and
  // says nothing at all about which line is 2015.
  auto* key = new QLabel(this);
  key->setTextFormat(Qt::RichText);
  key->setWordWrap(true);
  key->setContentsMargins(8, 4, 8, 0);
  connect(chart_, &SeasonChart::keyChanged, key, &QLabel::setText);
  key->setText(chart_->colour_key());

  auto* chart_side = new QWidget(this);
  auto* chart_column = new QVBoxLayout;
  chart_column->setContentsMargins(0, 0, 0, 0);
  chart_column->addWidget(key);
  chart_column->addWidget(chart_, 1);
  chart_side->setLayout(chart_column);

  auto* split = new QSplitter(Qt::Vertical, this);
  split->addWidget(table_);
  split->addWidget(chart_side);
  split->setStretchFactor(0, 3);
  split->setStretchFactor(1, 2);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
  QPushButton* page_button = buttons->addButton("Save page", QDialogButtonBox::ActionRole);
  QPushButton* indicators_button =
      buttons->addButton("Indicators as CSV", QDialogButtonBox::ActionRole);
  QPushButton* series_button = buttons->addButton("Series as CSV", QDialogButtonBox::ActionRole);

  auto* layout = new QVBoxLayout;
  layout->addWidget(trust);
  layout->addWidget(split, 1);
  layout->addWidget(buttons);
  setLayout(layout);
  resize(960, 900);

  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(page_button, &QPushButton::clicked, this, &DashboardDialog::save_page);
  connect(indicators_button, &QPushButton::clicked, this, &DashboardDialog::save_indicators);
  connect(series_button, &QPushButton::clicked, this, &DashboardDialog::save_series);
}

void DashboardDialog::build_table() {
  table_ = new QTableWidget(this);
  table_->setColumnCount(6);
  table_->setHorizontalHeaderLabels(
      {"Indicator", "Value", "Unit", "Expected", "Standing", "Trust"});
  table_->verticalHeader()->setVisible(false);
  table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table_->setSelectionBehavior(QAbstractItemView::SelectRows);
  table_->setAlternatingRowColors(true);

  int rows = 0;
  for (const config::DashboardPanel& panel : dashboard_.panels) {
    rows += 1 + static_cast<int>(panel.indicators.size());
  }
  table_->setRowCount(rows);

  int row = 0;
  for (const config::DashboardPanel& panel : dashboard_.panels) {
    // The panel heading, spanning the row, with the question it answers - so a
    // reader knows what they are looking at before they read a number.
    auto* heading =
        new QTableWidgetItem(QString::fromStdString(panel.title + " - " + panel.question));
    QFont bold = heading->font();
    bold.setBold(true);
    heading->setFont(bold);
    table_->setItem(row, 0, heading);
    table_->setSpan(row, 0, 1, 6);
    ++row;

    for (const config::Indicator& indicator : panel.indicators) {
      table_->setItem(row, 0, new QTableWidgetItem(QString::fromStdString(indicator.name)));

      auto* value = new QTableWidgetItem(figure(indicator));
      value->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
      table_->setItem(row, 1, value);

      table_->setItem(row, 2, new QTableWidgetItem(QString::fromStdString(indicator.unit)));
      table_->setItem(row, 3, new QTableWidgetItem(band_of(indicator)));

      auto* standing =
          new QTableWidgetItem(QString::fromStdString(config::to_string(indicator.standing)));
      if (const QColor colour = colour_for(indicator.standing); colour.isValid()) {
        standing->setForeground(QBrush(colour));
      }
      table_->setItem(row, 4, standing);

      auto* trust =
          new QTableWidgetItem(QString::fromStdString(config::to_string(indicator.trust)));
      // **The note lives on the row it qualifies.** A caveat in a separate
      // document is a caveat nobody reads with the number.
      trust->setToolTip(QString::fromStdString(indicator.note));
      table_->setItem(row, 5, trust);

      // And on the name as well, because that is the cell a reader hovers.
      table_->item(row, 0)->setToolTip(QString::fromStdString(indicator.note));
      ++row;
    }
  }

  table_->resizeColumnsToContents();
  table_->horizontalHeader()->setStretchLastSection(true);
}

void DashboardDialog::build_chart() {
  chart_ = new SeasonChart(this);

  std::vector<QString> dates;
  dates.reserve(dashboard_.dates.size());
  for (const core::Date& date : dashboard_.dates) {
    dates.push_back(QString::fromStdString(date.to_iso_string()));
  }

  std::vector<SeasonChart::Line> lines;
  lines.reserve(dashboard_.series.size());
  for (std::size_t i = 0; i < dashboard_.series.size(); ++i) {
    const config::DashboardSeries& series = dashboard_.series[i];
    SeasonChart::Line line;
    line.name = QString::fromStdString(series.name);
    line.unit = QString::fromStdString(series.unit);
    line.colour = line_colour(static_cast<int>(i));
    line.values = series.values;
    line.reference = series.reference;
    lines.push_back(std::move(line));
  }

  chart_->show_run(dates, lines, {});
}

void DashboardDialog::save_as(const QString& suggested, const QString& filter,
                              const QString& contents) {
  const QString path = QFileDialog::getSaveFileName(this, "Save", suggested, filter);
  if (path.isEmpty()) {
    return;
  }
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
    QMessageBox::warning(this, "Save", "Could not write " + path);
    return;
  }
  QTextStream out(&file);
  out << contents;
}

void DashboardDialog::save_indicators() {
  save_as(QString::fromStdString(dashboard_.farm + "-" + dashboard_.label + "-indicators.csv"),
          "CSV (*.csv)", QString::fromStdString(config::indicators_as_csv(dashboard_)));
}

void DashboardDialog::save_series() {
  save_as(QString::fromStdString(dashboard_.farm + "-" + dashboard_.label + "-series.csv"),
          "CSV (*.csv)", QString::fromStdString(config::series_as_csv(dashboard_)));
}

void DashboardDialog::save_page() {
  save_as(QString::fromStdString(dashboard_.farm + "-" + dashboard_.label + "-dashboard.txt"),
          "Text (*.txt)", QString::fromStdString(config::as_text(dashboard_)));
}

}  // namespace paddock::app
