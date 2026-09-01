// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#include "Theme.hpp"

#include <QColor>
#include <QPalette>
#include <QStyleFactory>

namespace paddock::app {

namespace {

/// The palette, named once so the style sheet and the palette agree.
///
/// A slate blue rather than a neutral grey: the scene's own background is a
/// blue-dark gradient, and a window built from the same family reads as one
/// instrument instead of a picture in a box.
constexpr auto kBackground = 0x141922;
constexpr auto kSurface = 0x1B2230;
constexpr auto kRaised = 0x232C3B;
constexpr auto kBorder = 0x33405A;
constexpr auto kText = 0xDCE4F2;
constexpr auto kDimText = 0x8A98B4;

/// The one colour that means "this is live". Used for focus, selection and the
/// controls that start something - and nowhere else, so it keeps meaning it.
constexpr auto kAccent = 0x3FA7DD;

QColor of(int rgb) {
  return QColor::fromRgb(static_cast<QRgb>(rgb));
}

QString hex(int rgb) {
  return QColor::fromRgb(static_cast<QRgb>(rgb)).name();
}

}  // namespace

void apply_theme(QApplication& application) {
  // Fusion, because the native Windows style ignores a palette in half the
  // places that matter - a themed window with system-coloured combo boxes in it
  // looks like a bug rather than a choice.
  QApplication::setStyle(QStyleFactory::create("Fusion"));

  QPalette palette;
  palette.setColor(QPalette::Window, of(kBackground));
  palette.setColor(QPalette::WindowText, of(kText));
  palette.setColor(QPalette::Base, of(kSurface));
  palette.setColor(QPalette::AlternateBase, of(kRaised));
  palette.setColor(QPalette::Text, of(kText));
  palette.setColor(QPalette::Button, of(kRaised));
  palette.setColor(QPalette::ButtonText, of(kText));
  palette.setColor(QPalette::Highlight, of(kAccent));
  palette.setColor(QPalette::HighlightedText, of(kBackground));
  palette.setColor(QPalette::ToolTipBase, of(kRaised));
  palette.setColor(QPalette::ToolTipText, of(kText));
  palette.setColor(QPalette::PlaceholderText, of(kDimText));
  palette.setColor(QPalette::Link, of(kAccent));
  // Disabled has to be visibly disabled. Left at the default it came out the
  // same shade as enabled text on this background, so a greyed-out button
  // looked like one that simply did not work when pressed.
  palette.setColor(QPalette::Disabled, QPalette::WindowText, of(kDimText));
  palette.setColor(QPalette::Disabled, QPalette::Text, of(kDimText));
  palette.setColor(QPalette::Disabled, QPalette::ButtonText, of(kDimText));
  QApplication::setPalette(palette);

  application.setStyleSheet(
      QString(
          "QGroupBox { border: 1px solid %BORDER%; border-radius: 8px; margin-top: 14px; "
          "  padding: 10px 8px 8px 8px; background: %SURFACE%; }"
          "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; "
          "  color: %DIM%; font-weight: 600; letter-spacing: 0.6px; text-transform: uppercase; }"

          // A heading over something that is not in a group box - the scenario
          // list - set to match the group titles above, so the side of the
          // window reads as one column of named sections.
          "QLabel#sectionHeading { color: %DIM%; font-weight: 600; letter-spacing: 0.6px; }"

          // The three panels under the map, each in a card of its own. The
          // chart already sat in one; the readings and the inspector did not,
          // and loose text beside a framed chart reads as a caption rather than
          // as a panel. Only the frame itself is styled - anything inside it
          // keeps the window's own background.
          "QFrame#panelCard { background: %SURFACE%; border: 1px solid %BORDER%; "
          "  border-radius: 8px; }"
          "QFrame#panelCard > QWidget { background: transparent; }"

          // An action on the panel it sits in rather than a call to arms: the
          // report buttons are these, at the foot of the readings they expand
          // on. Lighter than the button that starts a run, because they are not
          // that - but **quiet is not the same as invisible**, and this style
          // had crossed the line.
          //
          // Transparent over %DIM% text was hard enough to read. The disabled
          // rule was worse: it painted both the text and the border in
          // %SURFACE%, which is the colour of the card the buttons sit on, so a
          // disabled action was not dim - it was gone. Since these only enable
          // once a run has finished, the way into the indicators page was a
          // blank strip of panel until you happened to run something, and a
          // person looking for it before then had nothing to find.
          //
          // Now: a surface of its own so the shape reads as a button, full text
          // colour so the words read at a glance, and a disabled state that is
          // faint but still there.
          "QPushButton#quietAction { background: %SURFACE%; border: 1px solid %BORDER%; "
          "  border-radius: 6px; padding: 4px 12px; color: %TEXT%; min-height: 18px; }"
          "QPushButton#quietAction:hover:enabled { border-color: %ACCENT%; background: %RAISED%; }"
          "QPushButton#quietAction:disabled { color: %DIM%; border-color: %BORDER%; "
          "  background: transparent; }"

          // **A headline number and what stands behind it.**
          //
          // The dashboard's tiles. A card with a hairline top edge in the
          // accent, a caption in small caps above, the figure large, and the
          // provenance under it in the dim grey - so the eye lands on the
          // number and the next thing it finds is how far the number can be
          // trusted. That order is deliberate: a tile is the part of a page
          // that ends up quoted in a slide, and this project's whole discipline
          // is that a model output must not travel without its caveat.
          "QFrame#kpiTile { background: %SURFACE%; border: 1px solid %BORDER%; "
          "  border-top: 2px solid %ACCENT%; border-radius: 8px; }"
          "QLabel#kpiCaption { color: %DIM%; font-size: 10px; letter-spacing: 1px; }"
          "QLabel#kpiValue { color: %TEXT%; }"
          "QLabel#kpiUnit { color: %DIM%; font-size: 11px; padding-bottom: 4px; }"
          "QLabel#kpiTrust { color: %DIM%; font-size: 10px; font-style: italic; }"

          // The title on a chart card, quieter than the chart itself.
          "QLabel#cardTitle { color: %TEXT%; font-weight: 600; padding: 2px 4px; }"

          "QLabel#donutCentre { color: %TEXT%; font-weight: 600; }"

          // One height for every button in the window. Left to itself a button
          // is as tall as its own text, so a bold one - the default action -
          // stood a few pixels taller than the plain ones beside it, and a row
          // of them read as ragged rather than as a row.
          "QPushButton { background: %RAISED%; border: 1px solid %BORDER%; border-radius: 6px; "
          "  padding: 6px 14px; min-height: 22px; color: %TEXT%; }"
          "QPushButton:hover:enabled { border-color: %ACCENT%; }"
          "QPushButton:pressed:enabled { background: %BORDER%; }"
          "QPushButton:default:enabled { background: %ACCENT%; border-color: %ACCENT%; "
          "  color: %BG%; font-weight: 600; }"
          "QPushButton:disabled { color: %DIM%; border-color: %SURFACE%; }"
          // The flat ones are the "Advanced (2)" disclosures, which are links
          // rather than buttons and are not held to the row height above.
          "QPushButton:flat { background: transparent; border: none; color: %DIM%; "
          "  text-align: left; padding-left: 2px; min-height: 0; }"
          "QPushButton:flat:hover { color: %ACCENT%; }"

          "QComboBox, QAbstractSpinBox, QLineEdit { background: %BG%; border: 1px solid %BORDER%; "
          "  border-radius: 5px; padding: 4px 7px; color: %TEXT%; selection-background-color: "
          "  %ACCENT%; }"
          "QComboBox:focus, QAbstractSpinBox:focus { border-color: %ACCENT%; }"
          "QComboBox QAbstractItemView { background: %SURFACE%; border: 1px solid %BORDER%; "
          "  selection-background-color: %ACCENT%; selection-color: %BG%; }"

          // **The scenario list is the one place a row is a thing you own.** It
          // gets more room per row than a list usually would, a left edge that
          // lights up when selected, and no focus rectangle - the colour is the
          // signal.
          "QListWidget { background: %BG%; border: 1px solid %BORDER%; border-radius: 8px; "
          "  padding: 4px; outline: none; }"
          "QListWidget::item { padding: 9px 10px; border-radius: 6px; margin: 2px; "
          "  border-left: 3px solid transparent; color: %TEXT%; }"
          "QListWidget::item:hover { background: %SURFACE%; }"
          "QListWidget::item:selected { background: %RAISED%; border-left-color: %ACCENT%; "
          "  color: %TEXT%; }"

          "QSlider::groove:horizontal { height: 4px; background: %BORDER%; border-radius: 2px; }"
          "QSlider::sub-page:horizontal { background: %ACCENT%; border-radius: 2px; }"
          "QSlider::handle:horizontal { background: %TEXT%; width: 12px; margin: -5px 0; "
          "  border-radius: 6px; }"
          "QSlider::groove:vertical { width: 4px; background: %BORDER%; border-radius: 2px; }"
          "QSlider::handle:vertical { background: %TEXT%; height: 12px; margin: 0 -5px; "
          "  border-radius: 6px; }"

          "QCheckBox { spacing: 6px; }"
          "QCheckBox::indicator { width: 13px; height: 13px; border-radius: 3px; "
          "  border: 1px solid %BORDER%; background: %BG%; }"
          "QCheckBox::indicator:checked { background: %ACCENT%; border-color: %ACCENT%; }"
          "QCheckBox:disabled { color: %DIM%; }"

          "QDockWidget { titlebar-close-icon: none; }"
          "QDockWidget::title { background: %SURFACE%; padding: 7px; color: %DIM%; "
          "  font-weight: 600; letter-spacing: 0.6px; text-transform: uppercase; }"

          "QScrollArea { border: none; }"
          "QScrollBar:vertical { background: transparent; width: 9px; margin: 0; }"
          "QScrollBar::handle:vertical { background: %BORDER%; border-radius: 4px; "
          "  min-height: 30px; }"
          "QScrollBar::handle:vertical:hover { background: %DIM%; }"
          "QScrollBar::add-line, QScrollBar::sub-line { height: 0; width: 0; }"
          "QScrollBar:horizontal { background: transparent; height: 9px; }"
          "QScrollBar::handle:horizontal { background: %BORDER%; border-radius: 4px; "
          "  min-width: 30px; }"

          "QHeaderView::section { background: %RAISED%; color: %DIM%; border: none; "
          "  border-bottom: 1px solid %BORDER%; padding: 6px; font-weight: 600; }"
          "QTableWidget { background: %BG%; gridline-color: %BORDER%; border: 1px solid %BORDER%; "
          "  border-radius: 8px; }"
          "QTableWidget::item:selected { background: %RAISED%; color: %TEXT%; }"

          "QSplitter::handle { background: %BORDER%; }"
          "QSplitter::handle:horizontal { width: 3px; }"
          "QSplitter::handle:vertical { height: 3px; }"
          "QToolTip { background: %RAISED%; color: %TEXT%; border: 1px solid %BORDER%; "
          "  padding: 5px; }")
          .replace("%BG%", hex(kBackground))
          .replace("%SURFACE%", hex(kSurface))
          .replace("%RAISED%", hex(kRaised))
          .replace("%BORDER%", hex(kBorder))
          .replace("%TEXT%", hex(kText))
          .replace("%DIM%", hex(kDimText))
          .replace("%ACCENT%", hex(kAccent)));
}

}  // namespace paddock::app
