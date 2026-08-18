// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#include "ReportDialog.hpp"

#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QMessageBox>
#include <QPushButton>
#include <QTextStream>
#include <QVBoxLayout>
#include <utility>

namespace paddock::app {

ReportDialog::ReportDialog(QString markdown, QString suggested_filename, QWidget* parent)
    : QDialog(parent),
      markdown_(std::move(markdown)),
      suggested_filename_(std::move(suggested_filename)) {
  setWindowTitle("Paddock - run report");

  view_ = new QTextBrowser(this);
  view_->setMarkdown(markdown_);
  view_->setOpenExternalLinks(false);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
  QPushButton* save_button = buttons->addButton("Save as...", QDialogButtonBox::ActionRole);

  auto* layout = new QVBoxLayout;
  layout->addWidget(view_, 1);
  layout->addWidget(buttons);
  setLayout(layout);
  resize(820, 900);

  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(save_button, &QPushButton::clicked, this, &ReportDialog::save);
}

void ReportDialog::save() {
  const QString path = QFileDialog::getSaveFileName(this, "Save report", suggested_filename_,
                                                    "Markdown (*.md);;All files (*)");
  if (path.isEmpty()) {
    return;
  }

  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
    QMessageBox::warning(this, "Save report",
                         QString("Could not write %1: %2").arg(path, file.errorString()));
    return;
  }
  // The Markdown as it was generated, not the rendered page. What is saved has
  // to be the thing that can be re-rendered and diffed; saving the widget's
  // HTML would save Qt's idea of the report instead of the report.
  QTextStream out(&file);
  out << markdown_;
}

}  // namespace paddock::app
