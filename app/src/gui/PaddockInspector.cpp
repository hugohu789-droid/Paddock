// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#include "PaddockInspector.hpp"

#include <QString>
#include <QVBoxLayout>
#include <string>

#include <paddock/config/PaddockInspection.hpp>

namespace paddock::app {

namespace {

/// Anything a run could put in a label, made safe to sit in rich text.
///
/// A paddock's name comes from a survey and a held-back reason comes from the
/// schedule; neither is markup, and a name with an ampersand in it should read
/// as an ampersand rather than as the start of an entity.
QString escaped(const std::string& text) {
  return QString::fromStdString(text).toHtmlEscaped();
}

/// A row of the panel: what it is on the left, what it reads on the right.
QString row(const config::PanelRow& line) {
  QString html = QString("<tr><td>%1</td><td align='right'><b>%2</b></td></tr>")
                     .arg(escaped(line.label), escaped(line.value));
  if (!line.note.empty()) {
    // Under the figure, not beside it. A note in the right-hand column would
    // push the number a reader is looking for out of the line it lives on.
    html += QString(
                "<tr><td colspan='2' style='padding-bottom:2px'>"
                "<span style='color:#8A98B4; font-size:11px'>%1</span></td></tr>")
                .arg(escaped(line.note));
  }
  return html;
}

QString section(const config::PanelSection& group) {
  // Three pixels, not six. The gap only has to say "a new group starts here",
  // and four of them at six pixels was a line of text the panel could not
  // spare - which is how the sentence at the foot ended up half below the fold.
  QString html = QString(
                     "<tr><td colspan='2' style='padding-top:3px'>"
                     "<span style='color:#8A98B4'>%1</span></td></tr>")
                     .arg(escaped(group.title));
  if (!group.subtitle.empty()) {
    // **The subtitle is what stops the panel being misread.** "after today's
    // rain, growth and any irrigation" over one section and "decided this
    // morning, before any of the above" over the next are the difference
    // between two readings of the same soil and two contradictory facts.
    html += QString(
                "<tr><td colspan='2'>"
                "<span style='color:#8A98B4; font-size:11px'><i>%1</i></span></td></tr>")
                .arg(escaped(group.subtitle));
  }
  for (const config::PanelRow& line : group.rows) {
    html += row(line);
  }
  return html;
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
  body_->setToolTip({});
}

void PaddockInspector::show_paddock(const config::PaddockInspection& inspection,
                                    const std::string& grazing_rule) {
  // **Everything below is layout.** What goes in the panel, in what order, and
  // what each figure means is `config::inspector_panel`, which can be tested
  // without a window - and which is where the rule lives that a decision-time
  // reading and an end-of-day reading are never shown as the same fact.
  const config::InspectorPanel panel = config::inspector_panel(inspection, grazing_rule);

  heading_->setText(QString("<b>%1</b> &nbsp; <span style='color:#8A98B4'>%2</span>")
                        .arg(escaped(panel.heading), escaped(panel.subheading)));

  QString table = "<table width='100%' cellspacing='0' cellpadding='1'>";
  for (const config::PanelSection& group : panel.sections) {
    table += section(group);
  }
  table += "</table>";

  body_->setText(table);
  // The grazing rule is on hover: it is the same paragraph all year, and one
  // that never changes stops being read while still taking the room the day's
  // numbers need.
  body_->setToolTip(escaped(panel.grazing_rule));
}

}  // namespace paddock::app
