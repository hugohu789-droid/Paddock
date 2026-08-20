// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#pragma once

#include <QLabel>
#include <QWidget>
#include <optional>
#include <string>

#include <paddock/config/PaddockInspection.hpp>

namespace paddock::app {

/// One paddock's day, laid out to be read rather than parsed.
///
/// **A panel, not a line.** What this replaced was a single sentence of nine
/// figures separated by pipes, which is a debugging print: everything in it was
/// true and nothing in it could be found. The same figures under headings can
/// be read at a glance, and the two sentences at the foot are the only things
/// here that are not a measurement.
///
/// **No model arithmetic lives in this class.** It is handed a
/// `config::PaddockInspection` and formats it. Anything it would have to work
/// out for itself belongs on the other side of that struct, where it can be
/// tested without a window.
class PaddockInspector : public QWidget {
  Q_OBJECT

 public:
  explicit PaddockInspector(QWidget* parent = nullptr);

  /// Shows one paddock's day.
  void show_paddock(const config::PaddockInspection& inspection, const std::string& grazing_rule);

  /// Shows the state before anything has been clicked, and after a run that
  /// replaced whatever was being looked at.
  ///
  /// Stale figures under a fresh run would be the worst thing this panel could
  /// do, so there is one way to clear it and it is used from everywhere that
  /// invalidates a selection.
  void show_nothing_selected();

 private:
  QLabel* heading_ = nullptr;
  QLabel* body_ = nullptr;
};

}  // namespace paddock::app
