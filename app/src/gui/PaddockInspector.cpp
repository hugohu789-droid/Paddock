// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#include "PaddockInspector.hpp"

#include <QString>
#include <QVBoxLayout>
#include <optional>
#include <string>

namespace paddock::app {

namespace {

/// A figure, or a dash where the run kept none.
///
/// A dash rather than a zero, every time. "0.0 mm" says no water was put on;
/// an empty series says nobody wrote the number down, and the two send a reader
/// to different places.
QString figure(const std::optional<double>& value, int places, const QString& suffix) {
  if (!value.has_value()) {
    return "&mdash;";
  }
  return QString("%1%2").arg(*value, 0, 'f', places).arg(suffix);
}

QString percent(const std::optional<double>& fraction) {
  if (!fraction.has_value()) {
    return "&mdash;";
  }
  return QString("%1%").arg(*fraction * 100.0, 0, 'f', 0);
}

/// A row of the panel: what it is on the left, what it reads on the right.
QString row(const QString& name, const QString& value) {
  return QString("<tr><td>%1</td><td align='right'><b>%2</b></td></tr>").arg(name, value);
}

QString section(const QString& title) {
  // Three pixels, not six. The gap only has to say "a new group starts here",
  // and four of them at six pixels was a line of text the panel could not
  // spare - which is how the sentence at the foot ended up half below the fold.
  return QString(
             "<tr><td colspan='2' style='padding-top:3px'><span style='color:#8A98B4'>%1</span>"
             "</td></tr>")
      .arg(title);
}

}  // namespace

PaddockInspector::PaddockInspector(QWidget* parent) : QWidget(parent) {
  heading_ = new QLabel(this);
  heading_->setTextFormat(Qt::RichText);
  heading_->setWordWrap(true);

  body_ = new QLabel(this);
  body_->setTextFormat(Qt::RichText);
  body_->setWordWrap(true);
  body_->setAlignment(Qt::AlignTop | Qt::AlignLeft);

  auto* layout = new QVBoxLayout;
  // Its own column now, so it carries its own margins rather than borrowing the
  // readings' ones.
  layout->setContentsMargins(10, 8, 10, 8);
  layout->setSpacing(4);
  layout->addWidget(heading_);
  layout->addWidget(body_);
  setLayout(layout);

  show_nothing_selected();
}

void PaddockInspector::show_nothing_selected() {
  heading_->setText("<b>Paddock inspector</b>");
  body_->setText("Click a paddock on the map to inspect it.");
}

void PaddockInspector::show_paddock(const config::PaddockInspection& inspection,
                                    const std::string& grazing_rule) {
  heading_->setText(QString("<b>%1</b> &nbsp; <span style='color:#8A98B4'>%2 ha &middot; %3 "
                            "cells</span>")
                        .arg(QString::fromStdString(inspection.name))
                        .arg(inspection.hectares, 0, 'f', 1)
                        .arg(inspection.cells));

  QString table = "<table width='100%' cellspacing='0' cellpadding='1'>";

  // **Every value carries its unit.** They were on the section headings for a
  // while, which was compact and wrong: "Rested 85" against "Spell 35" reads as
  // a percentage as readily as a count of days, and a reader who has to work
  // out what a number is has stopped reading the panel.
  table += section("Pasture");
  table += row("Cover", figure(inspection.cover_kg_dm_per_ha, 0, " kg DM/ha"));
  table += row("Growth today", figure(inspection.growth_kg_dm_per_ha, 1, " kg DM/ha"));

  // **Now, and then what was decided this morning - kept apart.** They are two
  // states of the same soil and mixing them is how a panel comes to say the
  // farm watered ground that was already at 84%: it was at 45% when the
  // schedule looked, and the 84% is partly the water it put on.
  table += section("Current");
  table += row("Available water", percent(inspection.available_water_fraction));
  // **Named for what it does.** One means the soil held everything the grass
  // asked for; below one is the model saying growth was held back. Called
  // "stress", 1.00 reads as the worst day of the year.
  table += row("Water growth factor", figure(inspection.water_growth_factor, 2, ""));

  if (inspection.irrigation_enabled) {
    table += section("Today's irrigation");
    if (inspection.morning_water_fraction.has_value()) {
      table += row("Before irrigation", percent(inspection.morning_water_fraction));
    }
    table += row("Trigger",
                 QString("%1%").arg(inspection.irrigation_trigger_fraction * 100.0, 0, 'f', 0));
    table += row("Applied", figure(inspection.irrigation_today_mm, 1, " mm"));
    table +=
        row("Target", QString("%1%").arg(inspection.irrigation_target_fraction * 100.0, 0, 'f', 0));
    table += row("To date", figure(inspection.irrigation_to_date_mm, 0, " mm"));
  } else {
    table += section("Irrigation");
    table += row("Status", "Off in this scenario");
  }

  table += section("Grazing");
  table += row("Stock today", inspection.stock_today ? "On it" : "None");
  if (inspection.rest_days.has_value()) {
    table += row("Rested", QString("%1 days").arg(*inspection.rest_days));
  }
  if (inspection.minimum_spell_days > 0) {
    table += row("Minimum rest", QString("%1 days").arg(inspection.minimum_spell_days));
  }
  table += "</table>";

  // **The step between the figures, where there is one.** "at or below the
  // trigger" belongs between what the soil was and what went on it, which is
  // the order the schedule read them in. The grazing rule is on hover instead:
  // it is the same paragraph all year, and one that never changes stops being
  // read while still taking the room the day's numbers need.
  const std::string why = config::irrigation_reason_phrase(inspection);
  if (!why.empty()) {
    table += QString("<p style='color:#8A98B4; margin-top:6px; margin-bottom:0px'>&darr; %1</p>")
                 .arg(QString::fromStdString(why));
  }

  body_->setText(table);
  body_->setToolTip(QString::fromStdString(grazing_rule));
}

}  // namespace paddock::app
