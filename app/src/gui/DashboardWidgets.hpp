// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#pragma once

#include <QString>
#include <QWidget>
#include <optional>
#include <string>
#include <vector>

#include <paddock/config/FarmDashboard.hpp>

/// The pieces a dashboard page is built from.
///
/// **One chart type per question, rather than one chart for everything.** The
/// first version of the years page put ten years of one series on one line
/// chart and called it a dashboard. A line chart answers "how did this change
/// through the season" and nothing else: it cannot say how a year compares with
/// the nine around it (a bar does), where a budget went (a share does), whether
/// two quantities move together (a scatter does), or what this year's headline
/// figure actually is (a number does). Asking one chart all four questions is
/// how a page ends up saying none of them.
///
/// **And the reader is not one person.** A farmer opens this to find out
/// whether the farm fed its stock and what it cost; a researcher opens it to
/// find out whether the model is defensible and how much of it rests on
/// evidence; somebody writing a consent report opens it to find out whether the
/// farm would comply. Those are three different pages built from the same run,
/// and giving all three the same page serves none of them well.
namespace paddock::app::widgets {

/// The value of one indicator in one year, when that year has it.
[[nodiscard]] std::optional<config::Indicator> find_indicator(const config::FarmDashboard& board,
                                                              const std::string& name);

/// A single headline number: large, with its unit, its standing in colour, and
/// how far it can be trusted underneath.
///
/// **The trust caption is not optional and not a tooltip.** A tile is the most
/// quotable thing on the page - it is the bit that ends up in a slide - and a
/// tile reading "25.4 kg N/ha" with nothing beside it is an invitation to quote
/// a model output as a measurement.
[[nodiscard]] QWidget* kpi_tile(const QString& name, const QString& value, const QString& unit,
                                config::Standing standing, const QString& trust,
                                const QString& note);

/// The same tile, read straight off an indicator.
[[nodiscard]] QWidget* kpi_tile(const config::Indicator& indicator);

/// A reference drawn across a chart: a published figure, a regulatory trigger,
/// a mean. Named, because an undrawn line is a number nobody can check against.
struct Reference {
  QString name;
  double value = 0.0;
  QColor colour;
};

/// One bar per year for a single indicator, with any references drawn over it.
///
/// **The comparison a line chart cannot make.** "Did this year grow more than
/// the others" is a question about ten numbers, not about 3,650 days.
[[nodiscard]] QWidget* bars_by_year(const std::vector<config::FarmDashboard>& boards,
                                    const std::string& indicator, const QString& title,
                                    const std::vector<Reference>& references, bool colour_standing);

/// A share of a whole - where the water went, what the income was made of.
///
/// **Only ever used for things that genuinely partition.** A ring of quantities
/// that do not add up to anything is a decoration that reads as a budget, and
/// this project has budgets that really do close.
struct Slice {
  QString name;
  double value = 0.0;
  QColor colour;
};

[[nodiscard]] QWidget* donut(const QString& title, const QString& centre,
                             const std::vector<Slice>& slices);

/// One point per year: two indicators against each other, to show whether they
/// move together. The response of growth to rainfall is the one this was built
/// for, and it is the shape a rain-fed farm is judged by.
[[nodiscard]] QWidget* scatter_by_year(const std::vector<config::FarmDashboard>& boards,
                                       const std::string& x_indicator,
                                       const std::string& y_indicator, const QString& title);

/// A card with a title, for grouping. The page is a grid of these.
[[nodiscard]] QWidget* card(const QString& title, QWidget* body);

/// The colour a standing is drawn in, shared so a tile and a bar cannot
/// disagree about what "over" looks like.
[[nodiscard]] QColor standing_colour(config::Standing standing);

}  // namespace paddock::app::widgets
