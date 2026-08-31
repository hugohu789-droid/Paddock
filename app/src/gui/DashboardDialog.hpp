// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#pragma once

#include <QDialog>
#include <QString>
#include <QTableWidget>
#include <vector>

#include <paddock/config/FarmDashboard.hpp>

#include "SeasonChart.hpp"

namespace paddock::app {

/// The year's indicators and the trends behind them, on one page.
///
/// **Every row says how far it can be trusted, and that column is not
/// decoration.** A dashboard is the easiest place in this project to undo its
/// own discipline: a tile reading "27.3 kg N/ha" is read as a measurement, and
/// it is a model output resting on a placeholder. So `trust` is a column, the
/// notes are under the table rather than in a tooltip nobody opens, and a line
/// at the top says how many of the rows rest on evidence at all.
///
/// **The window computes nothing.** Everything shown is built by
/// `config::build_dashboard`, which the command line uses too - so a page and a
/// terminal cannot drift into describing different farms, which is exactly what
/// happened to the green cover series before the window was taught to fill it.
class DashboardDialog : public QDialog {
  Q_OBJECT

 public:
  DashboardDialog(config::FarmDashboard dashboard, QWidget* parent = nullptr);

 private slots:
  /// The indicators, with their standing, their band and their trust.
  void save_indicators();

  /// The daily series, one column each, for plotting elsewhere.
  void save_series();

  /// The page as text, which is what a person forwards.
  void save_page();

 private:
  void build_table();
  void build_chart();

  /// Saves `contents` under a name the user chooses, seeded from the farm and
  /// the year. Returns quietly if they cancel.
  void save_as(const QString& suggested, const QString& filter, const QString& contents);

  config::FarmDashboard dashboard_;
  QTableWidget* table_ = nullptr;
  SeasonChart* chart_ = nullptr;
};

}  // namespace paddock::app
