// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#pragma once

#include <QComboBox>
#include <QDialog>
#include <QTableWidget>
#include <QWidget>
#include <vector>

#include <paddock/config/FarmDashboard.hpp>

#include "SeasonChart.hpp"

namespace paddock::app {

/// The same farm across several years, read four ways.
///
/// **The comparison a single year cannot make, and the one this window did not
/// have.** `compare_dashboards_as_text` and `_as_csv` were written for exactly
/// this and only the command line ever called them: the indicators page was
/// built from one run, so a reader could see that this year leached 8.5 kg N/ha
/// and had no way to learn whether that was a good year or a bad one. On a farm
/// whose rainfall runs 527 mm to 1,036 mm that is most of the question.
///
/// **Four pages, because the reader is not one person.** This project is aimed
/// at farm consultants, research institutes and the people who write consent
/// reports, and those three open a dashboard looking for different things:
///
///   Farm         did it feed the stock, and what did that cost?
///   Model        is this defensible, and how much of it rests on evidence?
///   Environment  would this farm comply, and in which years would it not?
///   Indicators   every figure, year by year, for somebody who wants the lot
///
/// Serving all three from one page serves none of them, and the four are the
/// same data - `build_dashboard` output - arranged for a different question.
///
/// **And one chart type per question.** The first version of this page was ten
/// years of one series on one line chart. A line answers "how did this change
/// through the season" and nothing else: it cannot say how a year compares with
/// the nine around it - a bar does that - or where a budget went, which is a
/// share, or whether two quantities move together, which is a scatter, or what
/// the headline figure is, which is a number. Asking one chart all four
/// questions is how a page ends up answering none of them.
///
/// **This window still computes nothing.** The runs are made by whoever opened
/// it and handed here as built dashboards, so the page and the terminal cannot
/// drift into describing different farms.
class TrendDialog : public QDialog {
  Q_OBJECT

 public:
  explicit TrendDialog(std::vector<config::FarmDashboard> boards, QWidget* parent = nullptr);

 private slots:
  /// Redraws the season chart for whichever series the picker is on.
  void change_series(int index);

  /// The comparison as a page, which is what a person forwards.
  void save_page();

  /// One row per indicator, one column per year.
  void save_table();

 private:
  [[nodiscard]] QWidget* build_farm_page();
  [[nodiscard]] QWidget* build_model_page();
  [[nodiscard]] QWidget* build_environment_page();
  [[nodiscard]] QWidget* build_indicator_page();

  /// The season chart with its picker and its colour key, used by the farm
  /// page. Built once and owned by whichever page holds it.
  [[nodiscard]] QWidget* build_season_panel();

  void save_as(const QString& suggested, const QString& filter, const QString& contents);

  /// The mean of one indicator across every year that has it, which is the
  /// reference a single year is read against on this page.
  [[nodiscard]] double mean_of(const std::string& indicator) const;

  /// The most recent year, which is the one a tile shows.
  [[nodiscard]] const config::FarmDashboard& latest() const;

  std::vector<config::FarmDashboard> boards_;
  QTableWidget* table_ = nullptr;
  SeasonChart* chart_ = nullptr;
  QComboBox* series_picker_ = nullptr;
};

}  // namespace paddock::app
