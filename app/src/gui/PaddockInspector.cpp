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
  return QString(
             "<tr><td colspan='2' style='padding-top:6px'><span style='color:#8A98B4'>%1</span>"
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

  // **The unit is on the heading, once.** Repeated down the value column it
  // took more room than the numbers did, and in a column this narrow "3685 kg
  // DM/ha" wrapped onto a second line - which turns a table into a paragraph.
  table += section("Pasture &middot; kg DM/ha");
  table += row("Cover", figure(inspection.cover_kg_dm_per_ha, 0, ""));
  table += row("Growth today", figure(inspection.growth_kg_dm_per_ha, 1, ""));

  table += section("Water");
  table += row("Available water", percent(inspection.available_water_fraction));
  // **Named for what it does.** One means the soil held everything the grass
  // asked for; below one is the model saying growth was held back. Called
  // "stress", 1.00 reads as the worst day of the year.
  table += row("Water growth factor", figure(inspection.water_growth_factor, 2, ""));

  if (inspection.irrigation_enabled) {
    table += section("Irrigation &middot; mm");
    table += row("Today", figure(inspection.irrigation_today_mm, 1, ""));
    table += row("To date", figure(inspection.irrigation_to_date_mm, 0, ""));
  } else {
    table += section("Irrigation");
    table += row("Status", "Off");
  }

  table += section("Grazing &middot; days");
  table += row("Stock today", inspection.stock_today ? "On it" : "None");
  if (inspection.rest_days.has_value()) {
    table += row("Rested", QString::number(*inspection.rest_days));
  }
  if (inspection.minimum_spell_days > 0) {
    table += row("Spell aimed at", QString::number(inspection.minimum_spell_days));
  }
  table += "</table>";

  // **The water sentence is in the panel; the grazing rule is on hover.** One
  // changes every day and is the reason the figures above it are what they are.
  // The other is the same paragraph all year, and a paragraph that never
  // changes stops being read after the second day while still taking the room
  // that the day's numbers need.
  table += QString("<p style='color:#8A98B4'>%1</p>")
               .arg(QString::fromStdString(config::irrigation_sentence(inspection)));

  body_->setText(table);
  body_->setToolTip(QString::fromStdString(grazing_rule));
}

}  // namespace paddock::app
