// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#include "TrendDialog.hpp"

#include <QBrush>
#include <QColor>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFont>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QStringList>
#include <QTabWidget>
#include <QTextStream>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

#include "DashboardWidgets.hpp"

namespace paddock::app {

namespace {

using widgets::Reference;
using widgets::Slice;

/// One colour per year, ramped rather than picked from a fixed palette.
///
/// Ten years want ten distinguishable colours, and more than that they want an
/// *order*: a reader should be able to tell which end of the ramp is early and
/// which is late without going to the key. A fixed palette gives neither.
QColor year_colour(int index, int total) {
  const double t = total > 1 ? static_cast<double>(index) / static_cast<double>(total - 1) : 0.0;
  double hue = 0.58 - (0.42 * t);
  if (hue < 0.0) {
    hue += 1.0;
  }
  return QColor::fromHsvF(static_cast<float>(hue), 0.62F, 0.88F);
}

QString figure(const config::Indicator& indicator) {
  if (std::abs(indicator.value) >= 100.0 || indicator.unit == "head") {
    return QString::number(indicator.value, 'f', 0);
  }
  return QString::number(indicator.value, 'f', 1);
}

/// A row of tiles across the width of a page.
QWidget* tile_row(const std::vector<QWidget*>& tiles) {
  auto* holder = new QWidget;
  auto* row = new QHBoxLayout;
  row->setContentsMargins(0, 0, 0, 0);
  row->setSpacing(8);
  for (QWidget* tile : tiles) {
    row->addWidget(tile, 1);
  }
  holder->setLayout(row);
  return holder;
}

/// Tiles for whichever of `names` this year actually has. A page that asks for
/// an indicator the run did not produce shows one tile fewer rather than an
/// empty box - the money and flock panels only exist when a bundle was given
/// economics, and a page must not depend on that.
std::vector<QWidget*> tiles_for(const config::FarmDashboard& board,
                                const std::vector<std::string>& names) {
  std::vector<QWidget*> tiles;
  for (const std::string& name : names) {
    if (const std::optional<config::Indicator> one = widgets::find_indicator(board, name);
        one.has_value()) {
      tiles.push_back(widgets::kpi_tile(*one));
    }
  }
  return tiles;
}

/// Wraps a page in a scroll area, because a grid of charts is taller than a
/// dialog and a chart squeezed to nothing says less than no chart at all.
QWidget* scrolled(QWidget* page) {
  auto* area = new QScrollArea;
  area->setWidget(page);
  area->setWidgetResizable(true);
  area->setFrameShape(QFrame::NoFrame);
  return area;
}

}  // namespace

TrendDialog::TrendDialog(std::vector<config::FarmDashboard> boards, QWidget* parent)
    : QDialog(parent), boards_(std::move(boards)) {
  const QString farm =
      boards_.empty() ? QString("no years") : QString::fromStdString(boards_.front().farm);
  setWindowTitle("Paddock - " + farm + ", " + QString::number(boards_.size()) + " years");

  auto* caveat = new QLabel(
      "Each year is one run of this farm under the same management - only the weather differs. "
      "Every indicator keeps the trust it has on its own page: a figure that is a placeholder for "
      "one year is a placeholder for all of them, and a trend through placeholders is still a "
      "trend through placeholders.",
      this);
  caveat->setWordWrap(true);
  caveat->setObjectName("caveat");

  auto* tabs = new QTabWidget(this);
  if (!boards_.empty()) {
    tabs->addTab(scrolled(build_farm_page()), "Farm");
    tabs->addTab(scrolled(build_model_page()), "Model");
    tabs->addTab(scrolled(build_environment_page()), "Environment");
  }
  tabs->addTab(build_indicator_page(), "All indicators");

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
  QPushButton* page_button = buttons->addButton("Save comparison", QDialogButtonBox::ActionRole);
  QPushButton* csv_button = buttons->addButton("Years as CSV", QDialogButtonBox::ActionRole);

  auto* layout = new QVBoxLayout;
  layout->addWidget(caveat);
  layout->addWidget(tabs, 1);
  layout->addWidget(buttons);
  setLayout(layout);
  resize(1180, 940);

  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(page_button, &QPushButton::clicked, this, &TrendDialog::save_page);
  connect(csv_button, &QPushButton::clicked, this, &TrendDialog::save_table);
}

const config::FarmDashboard& TrendDialog::latest() const {
  return boards_.back();
}

double TrendDialog::mean_of(const std::string& indicator) const {
  double total = 0.0;
  int count = 0;
  for (const config::FarmDashboard& board : boards_) {
    if (const std::optional<config::Indicator> one = widgets::find_indicator(board, indicator);
        one.has_value()) {
      total += one->value;
      ++count;
    }
  }
  return count > 0 ? total / count : 0.0;
}

// ---------------------------------------------------------------- the farmer
//
// **What a farm consultant opens this for: did it feed the stock, and what did
// that cost.** Production, how much of it was eaten, the lowest the cover got,
// days short of feed - then the same years drawn as a season, because *when*
// the feed ran short matters as much as whether it did.
QWidget* TrendDialog::build_farm_page() {
  auto* page = new QWidget;
  auto* column = new QVBoxLayout;
  column->setContentsMargins(10, 10, 10, 10);
  column->setSpacing(10);

  auto* heading = new QLabel(
      QString("<b>%1</b> &mdash; the most recent year, with the ones before it to read it against.")
          .arg(QString::fromStdString(latest().label)));
  heading->setObjectName("caveat");
  column->addWidget(heading);

  column->addWidget(tile_row(tiles_for(latest(), {"Pasture grown", "Utilisation", "Lowest cover",
                                                  "Days short of feed", "Closing balance"})));

  const double mean_grown = mean_of("Pasture grown");
  auto* grown = widgets::bars_by_year(boards_, "Pasture grown", "Pasture grown, year by year",
                                      {Reference{QString("Mean %1").arg(mean_grown, 0, 'f', 0),
                                                 mean_grown, QColor(0x8A, 0x98, 0xB4)}},
                                      true);

  const double mean_used = mean_of("Utilisation");
  auto* used = widgets::bars_by_year(boards_, "Utilisation", "How much of it the stock ate",
                                     {Reference{QString("Mean %1%").arg(mean_used, 0, 'f', 0),
                                                mean_used, QColor(0x8A, 0x98, 0xB4)}},
                                     true);

  auto* charts = new QGridLayout;
  charts->setSpacing(10);
  charts->addWidget(widgets::card({}, grown), 0, 0);
  charts->addWidget(widgets::card({}, used), 0, 1);
  column->addLayout(charts);

  // **Where the year's growth went**, which is the farmer's real question and
  // one this model can answer honestly: what grew either reached an animal or
  // died where it stood.
  const std::optional<config::Indicator> grown_one =
      widgets::find_indicator(latest(), "Pasture grown");
  const std::optional<config::Indicator> used_one =
      widgets::find_indicator(latest(), "Utilisation");
  if (grown_one.has_value() && used_one.has_value()) {
    const double eaten = grown_one->value * used_one->value / 100.0;
    const double rest = std::max(0.0, grown_one->value - eaten);
    auto* split_charts = new QGridLayout;
    split_charts->setSpacing(10);
    split_charts->addWidget(
        widgets::card(
            {}, widgets::donut(QString("Where %1 went").arg(QString::fromStdString(latest().label)),
                               QString("%1 kg DM/ha in all").arg(grown_one->value, 0, 'f', 0),
                               {Slice{"Eaten by stock", eaten, QColor(0x3F, 0xB9, 0x60)},
                                Slice{"Died where it stood", rest, QColor(0x6B, 0x74, 0x8A)}})),
        0, 0);
    split_charts->addWidget(
        widgets::card({}, widgets::bars_by_year(boards_, "Lowest cover",
                                                "The lowest the farm got, year by year", {}, true)),
        0, 1);
    column->addLayout(split_charts);
  }

  column->addWidget(widgets::card("Every year through the season", build_season_panel()), 1);

  page->setLayout(column);
  return page;
}

// ------------------------------------------------------------- the researcher
//
// **What somebody at a research institute opens this for: is it defensible.**
// Not what the farm did - whether the model that says so can be quoted. The
// calibration against a measured trial, the water response a rain-fed farm is
// judged by, and a count of how much of the page rests on evidence at all.
QWidget* TrendDialog::build_model_page() {
  auto* page = new QWidget;
  auto* column = new QVBoxLayout;
  column->setContentsMargins(10, 10, 10, 10);
  column->setSpacing(10);

  const double mean_grown = mean_of("Pasture grown");
  const double mean_wue = mean_of("Water use efficiency");

  // **The two figures a calibration is judged on, next to what they are judged
  // against.** Winchmore's dryland treatment measured 6,442 kg DM/ha over 25
  // years; Martin et al. (2006) measured 12.3 kg DM/ha/mm at the same site.
  // See docs/validation/verify.md, E21 and E40.
  std::vector<QWidget*> tiles;
  tiles.push_back(widgets::kpi_tile(
      "Mean production", QString::number(mean_grown, 'f', 0), "kg DM/ha",
      config::Standing::Unmeasured, "model output",
      "Winchmore's dryland treatment measured 6,442 over 25 years on 745 mm. This model runs about "
      "20% above a trial that is fertilised where this farm is not - see verify.md E40."));
  tiles.push_back(widgets::kpi_tile(
      "Water use efficiency", QString::number(mean_wue, 'f', 1), "kg DM/ha/mm",
      config::Standing::Unmeasured, "fitted",
      "Radiation use efficiency was fitted to reproduce Martin et al. (2006): 12.3 for Canterbury "
      "dryland, 20 under irrigation. This is the fit, not an independent check of it."));
  tiles.push_back(widgets::kpi_tile(
      "Rests on evidence",
      QString("%1 / %2").arg(latest().indicators_on_evidence()).arg(latest().indicators_total()),
      "indicators", config::Standing::Unmeasured, "counted, not estimated",
      "Direct, derived or fitted. The rest are placeholders: the "
      "right order of magnitude and not measurements."));
  column->addWidget(tile_row(tiles));

  // The calibration itself: every year against the band the trial measured.
  column->addWidget(widgets::card(
      {}, widgets::bars_by_year(
              boards_, "Pasture grown", "Annual production against the Winchmore dryland trial",
              {Reference{"Winchmore mean 6,442", 6442.0, QColor(0x3F, 0xB9, 0x60)},
               Reference{"Its lowest year 3,904", 3904.0, QColor(0x8A, 0x98, 0xB4)},
               Reference{"Its highest 9,845", 9845.0, QColor(0x8A, 0x98, 0xB4)}},
              false)));

  // **The shape a rain-fed farm is judged by.** A model whose production did
  // not track its rainfall would be describing an irrigated farm however well
  // its mean landed.
  auto* charts = new QGridLayout;
  charts->setSpacing(10);
  charts->addWidget(widgets::card({}, widgets::scatter_by_year(boards_, "Rainfall", "Pasture grown",
                                                               "What each year's rain produced")),
                    0, 0);
  charts->addWidget(
      widgets::card({}, widgets::bars_by_year(boards_, "Days water-stressed",
                                              "Days the sward was short of water", {}, false)),
      0, 1);
  column->addLayout(charts);

  // **How much of this page is standing on anything.** Counted from the
  // indicators themselves rather than stated, so it cannot go stale.
  int direct = 0;
  int derived = 0;
  int fitted = 0;
  int verify = 0;
  int placeholder = 0;
  for (const config::Indicator& indicator : latest().all_indicators()) {
    switch (indicator.trust) {
      case config::Provenance::Direct:
        ++direct;
        break;
      case config::Provenance::Derived:
        ++derived;
        break;
      case config::Provenance::Fitted:
        ++fitted;
        break;
      case config::Provenance::Verify:
        ++verify;
        break;
      case config::Provenance::Placeholder:
        ++placeholder;
        break;
    }
  }
  column->addWidget(widgets::card(
      {},
      widgets::donut(
          "What the indicators rest on", QString("%1 indicators").arg(latest().indicators_total()),
          {Slice{"Direct", static_cast<double>(direct), QColor(0x3F, 0xB9, 0x60)},
           Slice{"Derived", static_cast<double>(derived), QColor(0x6F, 0xC0, 0x8A)},
           Slice{"Fitted", static_cast<double>(fitted), QColor(0x4C, 0x9A, 0xFF)},
           Slice{"Verify", static_cast<double>(verify), QColor(0xE0, 0xA0, 0x30)},
           Slice{"Placeholder", static_cast<double>(placeholder), QColor(0xE0, 0x5A, 0x4B)}})));

  column->addStretch(1);
  page->setLayout(column);
  return page;
}

// ------------------------------------------------------------- the regulator
//
// **What somebody writing a consent report opens this for: would it comply.**
// One year's leaching figure answers nothing on a farm whose drainage runs from
// 58 mm to 344 mm; what a report needs is every year against the rule, and the
// count of how many crossed it.
QWidget* TrendDialog::build_environment_page() {
  auto* page = new QWidget;
  auto* column = new QVBoxLayout;
  column->setContentsMargins(10, 10, 10, 10);
  column->setSpacing(10);

  // How many years crossed, taken from the standings rather than from a
  // threshold repeated here - the rule is whatever regulation was loaded, and
  // New Zealand sets them catchment by catchment.
  int over = 0;
  int measured = 0;
  for (const config::FarmDashboard& board : boards_) {
    if (const std::optional<config::Indicator> one =
            widgets::find_indicator(board, "Nitrate leached");
        one.has_value() && one->standing != config::Standing::Unmeasured) {
      ++measured;
      if (one->standing == config::Standing::Over) {
        ++over;
      }
    }
  }

  std::vector<QWidget*> tiles =
      tiles_for(latest(), {"Nitrate leached", "Drainage", "Nitrogen fixed", "Synthetic N applied"});
  if (measured > 0) {
    tiles.insert(tiles.begin(),
                 widgets::kpi_tile("Years over the trigger", QString::number(over),
                                   "of " + QString::number(measured),
                                   over > 0 ? config::Standing::Over : config::Standing::Good,
                                   "counted against the loaded rule",
                                   "New Zealand sets nitrogen limits catchment by catchment. This "
                                   "counts against whichever regulation was loaded and says "
                                   "nothing about any other zone."));
  }
  column->addWidget(tile_row(tiles));

  const double mean_leached = mean_of("Nitrate leached");
  column->addWidget(widgets::card(
      {}, widgets::bars_by_year(boards_, "Nitrate leached",
                                "Nitrate leached past the root zone, year by year",
                                {Reference{QString("Mean %1").arg(mean_leached, 0, 'f', 1),
                                           mean_leached, QColor(0x8A, 0x98, 0xB4)}},
                                true)));

  // **Leaching follows drainage**, and showing the two together is the whole
  // argument: a wet year leaches because the water carried it, not because the
  // farm did anything different.
  auto* charts = new QGridLayout;
  charts->setSpacing(10);
  charts->addWidget(
      widgets::card({}, widgets::scatter_by_year(boards_, "Drainage", "Nitrate leached",
                                                 "Leaching against the water that carried it")),
      0, 0);
  charts->addWidget(
      widgets::card({}, widgets::bars_by_year(boards_, "Drainage", "Water drained past the roots",
                                              {}, false)),
      0, 1);
  column->addLayout(charts);

  // **The nitrogen the farm handled, and the share of it that left.** A real
  // partition: this model closes its nitrogen budget to 1e-9.
  const std::optional<config::Indicator> fixed =
      widgets::find_indicator(latest(), "Nitrogen fixed");
  const std::optional<config::Indicator> excreta =
      widgets::find_indicator(latest(), "Excreta returned");
  const std::optional<config::Indicator> lost =
      widgets::find_indicator(latest(), "Nitrate leached");
  if (fixed.has_value() && excreta.has_value() && lost.has_value()) {
    const double handled = fixed->value + excreta->value;
    column->addWidget(widgets::card(
        {}, widgets::donut(QString("Nitrogen in %1").arg(QString::fromStdString(latest().label)),
                           QString("%1 kg N/ha handled").arg(handled, 0, 'f', 0),
                           {Slice{"Fixed by clover", fixed->value, QColor(0x3F, 0xB9, 0x60)},
                            Slice{"Returned in excreta", excreta->value, QColor(0x4C, 0x9A, 0xFF)},
                            Slice{"Leached to water", lost->value, QColor(0xE0, 0x5A, 0x4B)}})));
  }

  column->addStretch(1);
  page->setLayout(column);
  return page;
}

// --------------------------------------------------------------- everything
//
// The whole set, year by year, for a reader who wants the lot rather than the
// handful of figures a page chose for them.
QWidget* TrendDialog::build_indicator_page() {
  table_ = new QTableWidget;
  table_->verticalHeader()->setVisible(false);
  table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table_->setSelectionBehavior(QAbstractItemView::SelectRows);
  table_->setAlternatingRowColors(true);

  if (boards_.empty()) {
    return table_;
  }

  // **The first year's panels decide the rows.** Every board is the same farm
  // under the same management, so they carry the same indicators in the same
  // order. A year that somehow did not would leave a blank cell rather than
  // shifting every row beneath it out of line with its own name.
  const config::FarmDashboard& first = boards_.front();

  QStringList headers{"Indicator", "Unit"};
  for (const config::FarmDashboard& board : boards_) {
    headers << QString::fromStdString(board.label);
  }
  table_->setColumnCount(headers.size());
  table_->setHorizontalHeaderLabels(headers);

  int rows = 0;
  for (const config::DashboardPanel& panel : first.panels) {
    rows += 1 + static_cast<int>(panel.indicators.size());
  }
  table_->setRowCount(rows);

  int row = 0;
  for (std::size_t p = 0; p < first.panels.size(); ++p) {
    const config::DashboardPanel& panel = first.panels[p];

    auto* heading = new QTableWidgetItem(QString::fromStdString(panel.title));
    QFont bold = heading->font();
    bold.setBold(true);
    heading->setFont(bold);
    table_->setItem(row, 0, heading);
    table_->setSpan(row, 0, 1, headers.size());
    ++row;

    for (std::size_t i = 0; i < panel.indicators.size(); ++i) {
      const config::Indicator& indicator = panel.indicators[i];
      auto* name = new QTableWidgetItem(QString::fromStdString(indicator.name));
      name->setToolTip(QString::fromStdString(indicator.note));
      table_->setItem(row, 0, name);
      table_->setItem(row, 1, new QTableWidgetItem(QString::fromStdString(indicator.unit)));

      for (std::size_t y = 0; y < boards_.size(); ++y) {
        const std::vector<config::DashboardPanel>& panels = boards_[y].panels;
        if (p >= panels.size() || i >= panels[p].indicators.size()) {
          continue;
        }
        const config::Indicator& one = panels[p].indicators[i];
        auto* cell = new QTableWidgetItem(figure(one));
        cell->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        if (one.standing != config::Standing::Unmeasured) {
          cell->setForeground(QBrush(widgets::standing_colour(one.standing)));
        }
        cell->setToolTip(QString::fromStdString(one.note));
        table_->setItem(row, static_cast<int>(y) + 2, cell);
      }
      ++row;
    }
  }

  table_->resizeColumnsToContents();
  // **The slack goes into the names, not into whichever year happens to be
  // last.** Stretching the last section put the whole width of a two-year page
  // into the final column and left the indicator names clipped, which is the
  // one column a reader has to read.
  table_->horizontalHeader()->setStretchLastSection(false);
  table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
  return table_;
}

QWidget* TrendDialog::build_season_panel() {
  chart_ = new SeasonChart;
  chart_->setMinimumHeight(280);
  series_picker_ = new QComboBox;

  for (const config::DashboardSeries& series : boards_.front().series) {
    series_picker_->addItem(QString::fromStdString(series.name));
  }

  auto* picker_row = new QHBoxLayout;
  picker_row->addWidget(new QLabel("Every year's"));
  picker_row->addWidget(series_picker_);
  picker_row->addWidget(new QLabel("through the season"));
  picker_row->addStretch(1);

  // **Which colour is which**, which SeasonChart emits and this page was not
  // listening for. Ten years of one quantity make that unmissable: the axis
  // says kg DM/ha and says nothing at all about which line is 2015.
  auto* key = new QLabel;
  key->setTextFormat(Qt::RichText);
  key->setWordWrap(true);
  key->setContentsMargins(4, 2, 4, 0);
  connect(chart_, &SeasonChart::keyChanged, key, &QLabel::setText);

  change_series(0);
  key->setText(chart_->colour_key());

  auto* holder = new QWidget;
  auto* column = new QVBoxLayout;
  column->setContentsMargins(0, 0, 0, 0);
  column->addLayout(picker_row);
  column->addWidget(key);
  column->addWidget(chart_, 1);
  holder->setLayout(column);

  connect(series_picker_, &QComboBox::currentIndexChanged, this, &TrendDialog::change_series);
  return holder;
}

void TrendDialog::change_series(int index) {
  if (boards_.empty() || index < 0 || chart_ == nullptr) {
    return;
  }
  const auto which = static_cast<std::size_t>(index);
  if (which >= boards_.front().series.size()) {
    return;
  }

  // **The axis is the longest year and every line is drawn over it.** A farm
  // year containing a 29th of February is 366 days and the others are 365, so
  // an axis taken from a short year would drop the last day of a long one. The
  // dates are only ever an axis here: what a reader compares is the shape of a
  // season, not which calendar day a given point fell on.
  std::size_t longest = 0;
  for (const config::FarmDashboard& board : boards_) {
    longest = std::max(longest, board.dates.size());
  }

  std::vector<QString> dates;
  dates.reserve(longest);
  for (const config::FarmDashboard& board : boards_) {
    if (board.dates.size() == longest) {
      for (const core::Date& date : board.dates) {
        dates.push_back(QString::fromStdString(date.to_iso_string()));
      }
      break;
    }
  }

  std::vector<SeasonChart::Line> lines;
  lines.reserve(boards_.size());
  for (std::size_t y = 0; y < boards_.size(); ++y) {
    if (which >= boards_[y].series.size()) {
      continue;
    }
    const config::DashboardSeries& series = boards_[y].series[which];
    SeasonChart::Line line;
    // The year is the name, because the quantity is the same on every line and
    // the year is the only thing that tells them apart.
    line.name = QString::fromStdString(boards_[y].label);
    line.unit = QString::fromStdString(series.unit);
    line.colour = year_colour(static_cast<int>(y), static_cast<int>(boards_.size()));
    line.values = series.values;
    // **Only the first year carries the reference.** It is the same level for
    // every one of them - a grazing floor does not move with the weather - and
    // drawn per line it would be ten dashed lines on top of each other.
    if (y == 0) {
      line.reference = series.reference;
    }
    lines.push_back(std::move(line));
  }

  chart_->show_run(dates, lines, {}, SeasonChart::Scale::Shared);
}

void TrendDialog::save_as(const QString& suggested, const QString& filter,
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

void TrendDialog::save_page() {
  if (boards_.empty()) {
    return;
  }
  save_as(QString::fromStdString(boards_.front().farm) + "-years.txt", "Text (*.txt)",
          QString::fromStdString(config::compare_dashboards_as_text(boards_)));
}

void TrendDialog::save_table() {
  if (boards_.empty()) {
    return;
  }
  save_as(QString::fromStdString(boards_.front().farm) + "-years.csv", "CSV (*.csv)",
          QString::fromStdString(config::compare_dashboards_as_csv(boards_)));
}

}  // namespace paddock::app
