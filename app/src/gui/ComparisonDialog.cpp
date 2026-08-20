// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#include "ComparisonDialog.hpp"

#include <QApplication>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPageSize>
#include <QPdfWriter>
#include <QPushButton>
#include <QTextDocument>
#include <QTextStream>
#include <QVBoxLayout>
#include <cstddef>
#include <string>
#include <utility>

namespace paddock::app {

namespace {

/// A number as the row said to write it, so significance travels with the
/// quantity rather than with whoever is printing it.
QString number(double value, int decimals) {
  return QString::number(value, 'f', decimals);
}

/// Rows the table itself supplies as headings rather than as data.
QTableWidgetItem* heading(const QString& text) {
  auto* item = new QTableWidgetItem(text);
  QFont bold = item->font();
  bold.setBold(true);
  item->setFont(bold);
  item->setFlags(Qt::ItemIsEnabled);
  return item;
}

}  // namespace

ComparisonDialog::ComparisonDialog(config::ComparisonTable table, QWidget* parent)
    : QDialog(parent), table_(std::move(table)) {
  setWindowTitle("Scenario comparison");

  grid_ = new QTableWidget(this);
  grid_->setColumnCount(static_cast<int>(table_.scenarios.size()) + 1);

  QStringList headers;
  headers << "";
  for (const std::string& name : table_.scenarios) {
    headers << QString::fromStdString(name);
  }
  grid_->setHorizontalHeaderLabels(headers);
  grid_->verticalHeader()->setVisible(false);
  grid_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
  for (int column = 1; column < grid_->columnCount(); ++column) {
    grid_->horizontalHeader()->setSectionResizeMode(column, QHeaderView::Stretch);
  }
  // Read, not edited. A comparison somebody could type over would be a table
  // that no longer matched the runs it came from.
  grid_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  grid_->setSelectionBehavior(QAbstractItemView::SelectRows);

  int row = 0;
  const auto add_row = [this, &row](const QString& label, const QStringList& values, bool numeric) {
    grid_->insertRow(row);
    grid_->setItem(row, 0, new QTableWidgetItem(label));
    for (int column = 0; column < values.size(); ++column) {
      auto* item = new QTableWidgetItem(values[column]);
      if (numeric) {
        item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
      }
      grid_->setItem(row, column + 1, item);
    }
    ++row;
  };

  const auto add_heading = [this, &row](const QString& text) {
    grid_->insertRow(row);
    grid_->setItem(row, 0, heading(text));
    for (int column = 1; column < grid_->columnCount(); ++column) {
      grid_->setItem(row, column, heading(""));
    }
    ++row;
  };

  // **What differs comes first, because it is what the table is about.** A
  // reader who does not know what was changed cannot read the numbers under it
  // as anything but a list.
  if (!table_.differences.empty()) {
    add_heading("What differs");
    for (const config::SettingRow& setting : table_.differences) {
      QStringList values;
      for (const std::string& value : setting.values) {
        values << QString::fromStdString(value);
      }
      add_row(QString::fromStdString(setting.name), values, false);
    }
  } else {
    add_heading("Every setting is the same in all of these");
  }

  QString group;
  for (const config::MetricRow& metric : table_.metrics) {
    if (QString::fromStdString(metric.group) != group) {
      group = QString::fromStdString(metric.group);
      add_heading(group);
    }
    QStringList values;
    for (const double value : metric.values) {
      values << number(value, metric.decimals);
    }
    QString label = QString::fromStdString(metric.name);
    if (!metric.unit.empty()) {
      label += QString(" (%1)").arg(QString::fromStdString(metric.unit));
    }
    add_row(label, values, true);
  }

  summary_ = new QTextBrowser(this);
  QString words = QString::fromStdString(config::summarise(table_)).toHtmlEscaped();
  words.replace('\n', "<br>");
  QString caveats;
  for (const std::string& caveat : table_.caveats) {
    caveats += "<li>" + QString::fromStdString(caveat).toHtmlEscaped() + "</li>";
  }
  summary_->setHtml("<p>" + words + "</p><p><b>What this cannot tell you</b></p><ul>" + caveats +
                    "</ul>");
  summary_->setMinimumHeight(150);

  auto* pdf = new QPushButton("Save as PDF", this);
  auto* copy = new QPushButton("Copy as Markdown", this);
  auto* save = new QPushButton("Save as CSV", this);
  connect(pdf, &QPushButton::clicked, this, &ComparisonDialog::save_pdf);
  connect(copy, &QPushButton::clicked, this, &ComparisonDialog::copy_markdown);
  connect(save, &QPushButton::clicked, this, &ComparisonDialog::save_csv);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
  buttons->addButton(pdf, QDialogButtonBox::ActionRole);
  buttons->addButton(copy, QDialogButtonBox::ActionRole);
  buttons->addButton(save, QDialogButtonBox::ActionRole);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

  auto* layout = new QVBoxLayout;
  layout->addWidget(grid_, 3);
  layout->addWidget(summary_, 1);
  layout->addWidget(buttons);
  setLayout(layout);
  resize(900, 700);
}

namespace {

/// The report as a page: the table, then what it says, then what it cannot.
QString as_page(const config::ComparisonTable& table) {
  QString html =
      "<h2>Scenario comparison</h2><table cellspacing='0' cellpadding='4' border='1' "
      "width='100%'><tr><th align='left'></th>";
  for (const std::string& name : table.scenarios) {
    html += "<th align='right'>" + QString::fromStdString(name).toHtmlEscaped() + "</th>";
  }
  html += "</tr>";

  const auto heading_row = [&table](const QString& text) {
    QString row = "<tr><td colspan='" + QString::number(table.scenarios.size() + 1) + "'><b>" +
                  text.toHtmlEscaped() + "</b></td></tr>";
    return row;
  };

  if (!table.differences.empty()) {
    html += heading_row("What differs");
    for (const config::SettingRow& setting : table.differences) {
      html += "<tr><td>" + QString::fromStdString(setting.name).toHtmlEscaped() + "</td>";
      for (const std::string& value : setting.values) {
        html += "<td align='right'>" + QString::fromStdString(value).toHtmlEscaped() + "</td>";
      }
      html += "</tr>";
    }
  }

  QString group;
  for (const config::MetricRow& metric : table.metrics) {
    if (QString::fromStdString(metric.group) != group) {
      group = QString::fromStdString(metric.group);
      html += heading_row(group);
    }
    QString label = QString::fromStdString(metric.name).toHtmlEscaped();
    if (!metric.unit.empty()) {
      label += " (" + QString::fromStdString(metric.unit).toHtmlEscaped() + ")";
    }
    html += "<tr><td>" + label + "</td>";
    for (const double value : metric.values) {
      html += "<td align='right'>" + number(value, metric.decimals) + "</td>";
    }
    html += "</tr>";
  }
  html += "</table>";

  QString words = QString::fromStdString(config::summarise(table)).toHtmlEscaped();
  words.replace('\n', "<br>");
  html += "<p>" + words + "</p><p><b>What this cannot tell you</b></p><ul>";
  for (const std::string& caveat : table.caveats) {
    html += "<li>" + QString::fromStdString(caveat).toHtmlEscaped() + "</li>";
  }
  html += "</ul>";
  return html;
}

}  // namespace

void ComparisonDialog::save_pdf() {
  const QString path =
      QFileDialog::getSaveFileName(this, "Save report", "comparison.pdf", "PDF (*.pdf)");
  if (path.isEmpty()) {
    return;
  }

  // **Laid out again for the page rather than screenshotted.** A picture of the
  // window would carry whatever was scrolled into view and nothing else, at
  // whatever size the window happened to be. This is the same table, the same
  // summary and the same caveats, in the order they are meant to be read, at a
  // size a page can hold.
  QPdfWriter writer(path);
  writer.setPageSize(QPageSize(QPageSize::A4));
  writer.setPageMargins(QMarginsF(15, 15, 15, 15), QPageLayout::Millimeter);
  writer.setTitle("Paddock scenario comparison");

  QTextDocument document;
  document.setPageSize(QSizeF(writer.width(), writer.height()));
  document.setHtml(as_page(table_));
  document.print(&writer);
}

void ComparisonDialog::copy_markdown() {
  QApplication::clipboard()->setText(QString::fromStdString(config::as_markdown(table_)));
}

void ComparisonDialog::save_csv() {
  const QString path =
      QFileDialog::getSaveFileName(this, "Save comparison", "comparison.csv", "CSV (*.csv)");
  if (path.isEmpty()) {
    return;
  }
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
    QMessageBox::warning(this, "Save comparison", "Could not write " + path);
    return;
  }
  QTextStream out(&file);
  out << QString::fromStdString(config::as_csv(table_));
}

}  // namespace paddock::app
