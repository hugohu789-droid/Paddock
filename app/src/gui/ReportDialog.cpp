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

#include "PagePrinter.hpp"

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
  QPushButton* pdf_button = buttons->addButton("Save as PDF", QDialogButtonBox::ActionRole);
  QPushButton* save_button = buttons->addButton("Save as Markdown", QDialogButtonBox::ActionRole);

  auto* layout = new QVBoxLayout;
  layout->addWidget(view_, 1);
  layout->addWidget(buttons);
  setLayout(layout);
  resize(820, 900);

  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(save_button, &QPushButton::clicked, this, &ReportDialog::save);
  connect(pdf_button, &QPushButton::clicked, this, &ReportDialog::save_pdf);
}

void ReportDialog::save_pdf() {
  QString suggested = suggested_filename_;
  if (suggested.endsWith(".md", Qt::CaseInsensitive)) {
    suggested.chop(3);
  }
  const QString path =
      QFileDialog::getSaveFileName(this, "Save report", suggested + ".pdf", "PDF (*.pdf)");
  if (path.isEmpty()) {
    return;
  }

  // **Laid out for the page from the same Markdown, not screenshotted.** A
  // picture of the window would carry whatever was scrolled into view and
  // nothing else. Rendering the report again gives the whole of it, in the
  // order it is meant to be read.
  if (!print_markdown_to_pdf(path, markdown_, "Paddock run report")) {
    QMessageBox::warning(this, "Save report", "Could not write " + path);
  }
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
