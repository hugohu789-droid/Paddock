// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#pragma once

#include <QDialog>
#include <QString>
#include <QTextBrowser>

namespace paddock::app {

/// The run's report, as a page rather than as a file.
///
/// The report is Markdown because that is what a language model will be handed
/// when the templated version is replaced, and because it diffs. Qt renders
/// Markdown directly, so the same text serves the window and the file with no
/// second rendering path to keep in step - which matters, because the moment
/// there are two the screen and the saved file start disagreeing about what the
/// run said.
class ReportDialog : public QDialog {
  Q_OBJECT

 public:
  ReportDialog(QString markdown, QString suggested_filename, QWidget* parent = nullptr);

 private slots:
  void save();

 private:
  QString markdown_;
  QString suggested_filename_;
  QTextBrowser* view_ = nullptr;
};

}  // namespace paddock::app
