// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#include "PagePrinter.hpp"

#include <QBrush>
#include <QColor>
#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QMarginsF>
#include <QPageLayout>
#include <QPageSize>
#include <QPdfWriter>
#include <QTextDocument>
#include <QTextFrame>
#include <QTextTable>

namespace paddock::app {

namespace {

/// Dots per inch the page is laid out at.
///
/// Three hundred is a print resolution rather than a screen one, and low enough
/// that a page of text is a few hundred kilobytes rather than several megabytes.
constexpr int kPrintResolution = 300;

/// Margins in millimetres. Fifteen is what a report is normally bound and
/// hole-punched at, and leaves a line length that does not tire the eye.
constexpr double kMarginMm = 15.0;

/// Body size in points. Set here rather than left to the default, because the
/// default is whatever the machine's interface font happens to be - which on a
/// high-resolution display is a size chosen for a screen a foot away, not for
/// paper.
constexpr int kBodyPoints = 10;

/// How a table is ruled and spaced on the page, in points, and how much of the
/// width the first column takes.
///
/// **In points, converted, because the document is not laid out in points.** It
/// is laid out at the writer's resolution, so a border of "0.5" is half of a
/// three-hundredth of an inch - which is a tenth of a point, and disappears.
/// The first version of this set exactly that and the borders vanished from the
/// page altogether.
///
/// The first column holds the row names - "Days growth held back by dry soil" -
/// and the rest hold figures. Split evenly they would give a column of two-digit
/// numbers as much room as that sentence.
constexpr qreal kTableBorderPoints = 0.75;
constexpr qreal kTableCellPaddingPoints = 3.5;
constexpr auto kTableRule = 0x808080;
constexpr qreal kFirstColumnPercent = 40.0;

/// Points to the units the document is laid out in.
constexpr qreal points_to_units(qreal points, int resolution) {
  return points * resolution / 72.0;
}

/// Makes every table in the document run the full width of the page.
///
/// **Tables left to their natural width do not line up with each other.** Qt
/// sizes each one to its own contents, so a report with three tables in it
/// prints three different rectangles down the page - the reader's eye has
/// nothing to run along, and a column of figures in one does not sit under the
/// same column in the next. A page is a fixed width; the tables on it should be
/// too.
///
/// The borders and padding are set here as well, for the same reason: a table
/// from Markdown and a table from HTML arrive with different defaults, and the
/// two reports would print in two styles.
void align_tables(QTextDocument& document, int resolution) {
  // Walked through the root frame's children, which is where tables live.
  QTextFrame* root = document.rootFrame();
  if (root == nullptr) {
    return;
  }
  for (QTextFrame::iterator it = root->begin(); !it.atEnd(); ++it) {
    auto* table = qobject_cast<QTextTable*>(it.currentFrame());
    if (table == nullptr) {
      continue;
    }
    QTextTableFormat format = table->format();
    format.setWidth(QTextLength(QTextLength::PercentageLength, 100.0));
    format.setBorder(points_to_units(kTableBorderPoints, resolution));
    format.setBorderStyle(QTextFrameFormat::BorderStyle_Solid);
    format.setBorderBrush(QBrush(QColor(kTableRule)));
    format.setCellPadding(points_to_units(kTableCellPaddingPoints, resolution));
    format.setCellSpacing(0.0);
    // The first column carries the row names and the rest carry figures, so the
    // names get the room and the figures share what is left evenly. Left to
    // itself Qt gives every column the same width, and a column of two-digit
    // numbers ends up as wide as one holding "Days growth held back by dry
    // soil".
    QList<QTextLength> widths;
    widths.append(QTextLength(QTextLength::PercentageLength, kFirstColumnPercent));
    const int rest = table->columns() - 1;
    if (rest > 0) {
      const qreal each = (100.0 - kFirstColumnPercent) / rest;
      for (int column = 0; column < rest; ++column) {
        widths.append(QTextLength(QTextLength::PercentageLength, each));
      }
    }
    format.setColumnWidthConstraints(widths);
    table->setFormat(format);
  }
}

bool print_document(const QString& path, const QString& title, QTextDocument& document) {
  // **The directory is checked before the writer is built.** QPdfWriter takes a
  // file name and reports nothing when it cannot open it, so a path into a
  // folder that does not exist would produce no file, no error, and a person
  // going to look for it.
  if (!QFileInfo(path).absoluteDir().exists()) {
    return false;
  }

  QPdfWriter writer(path);
  writer.setPageSize(QPageSize(QPageSize::A4));
  writer.setPageMargins(QMarginsF(kMarginMm, kMarginMm, kMarginMm, kMarginMm),
                        QPageLayout::Millimeter);
  writer.setResolution(kPrintResolution);
  writer.setTitle(title);

  QFont body("Helvetica");
  body.setPointSize(kBodyPoints);
  document.setDefaultFont(body);

  align_tables(document, kPrintResolution);

  // **No setPageSize on the document.** print() fits it to the paged device
  // itself; giving it the writer's pixel dimensions lays the text out on a page
  // measured in three-hundredths of an inch while the fonts stay in points, and
  // the whole report comes out as a stamp in one corner.
  document.print(&writer);
  return true;
}

}  // namespace

bool print_markdown_to_pdf(const QString& path, const QString& markdown, const QString& title) {
  QTextDocument document;
  document.setMarkdown(markdown);
  return print_document(path, title, document);
}

bool print_html_to_pdf(const QString& path, const QString& html, const QString& title) {
  QTextDocument document;
  document.setHtml(html);
  return print_document(path, title, document);
}

}  // namespace paddock::app
