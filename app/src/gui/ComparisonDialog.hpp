// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#pragma once

#include <QDialog>
#include <QTableWidget>
#include <QTextBrowser>

#include <paddock/config/ScenarioComparison.hpp>

namespace paddock::app {

/// Several runs of the same model, side by side.
///
/// A window rather than a panel because a comparison is read, not driven: five
/// scenarios and twenty rows do not fit beside a map, and squeezing them there
/// would make both unreadable.
///
/// It renders `config::ComparisonTable` and computes nothing. Everything on
/// screen - including which settings differed and what the summary says - was
/// worked out in `config`, where it can be tested without a window and exported
/// without one.
class ComparisonDialog : public QDialog {
  Q_OBJECT

 public:
  explicit ComparisonDialog(config::ComparisonTable table, QWidget* parent = nullptr);

 private:
  config::ComparisonTable table_;
  QTableWidget* grid_ = nullptr;
  QTextBrowser* summary_ = nullptr;

  /// Puts the table on the clipboard as Markdown, ready for a report.
  void copy_markdown();

  /// Writes the report to a file the person chooses, as PDF.
  ///
  /// The one format that can be sent to somebody who does not have this
  /// program: a table, what differed, what the numbers say and what they cannot
  /// say, on one page, in the order they are meant to be read.
  void save_pdf();

  /// Writes the table to a file the person chooses, as CSV.
  void save_csv();
};

}  // namespace paddock::app
