// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include <paddock/viz/ColourScale.hpp>

namespace paddock::viz {

namespace {

struct Stop {
  double position = 0.0;
  Rgb colour;
};

/// Viridis, sampled at nine points from matplotlib 3.11's published colormap.
///
/// Viridis was designed to be perceptually uniform and readable with the common
/// colour vision deficiencies; nine anchors interpolated linearly reproduce it
/// closely enough for a map legend, and keep the table small enough to read.
constexpr std::array<Stop, 9> kViridis = {{
    {0.0000, {68, 1, 84}},
    {0.1250, {71, 45, 123}},
    {0.2500, {59, 82, 139}},
    {0.3750, {44, 114, 142}},
    {0.5000, {33, 145, 140}},
    {0.6250, {40, 174, 128}},
    {0.7500, {94, 201, 98}},
    {0.8750, {173, 220, 48}},
    {1.0000, {253, 231, 37}},
}};

/// Pale straw through to dark green, the way a farm pasture map is usually
/// drawn. Chosen for recognition, not for measurement.
constexpr std::array<Stop, 4> kPastureGreen = {{
    {0.0000, {237, 226, 189}},
    {0.3333, {186, 211, 140}},
    {0.6667, {104, 168, 84}},
    {1.0000, {27, 94, 32}},
}};

std::uint8_t blend(std::uint8_t from, std::uint8_t to, double weight) noexcept {
  const double value = static_cast<double>(from) + ((static_cast<double>(to) - from) * weight);
  return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0, 255.0)));
}

template <std::size_t N>
Rgb interpolate(const std::array<Stop, N>& stops, double position) noexcept {
  if (position <= stops.front().position) {
    return stops.front().colour;
  }
  if (position >= stops.back().position) {
    return stops.back().colour;
  }
  for (std::size_t i = 1; i < stops.size(); ++i) {
    if (position <= stops[i].position) {
      const Stop& low = stops[i - 1];
      const Stop& high = stops[i];
      const double span = high.position - low.position;
      const double weight = span > 0.0 ? (position - low.position) / span : 0.0;
      return Rgb{blend(low.colour.red, high.colour.red, weight),
                 blend(low.colour.green, high.colour.green, weight),
                 blend(low.colour.blue, high.colour.blue, weight)};
    }
  }
  return stops.back().colour;
}

}  // namespace

bool operator==(const Rgb& lhs, const Rgb& rhs) noexcept {
  return lhs.red == rhs.red && lhs.green == rhs.green && lhs.blue == rhs.blue;
}

bool operator!=(const Rgb& lhs, const Rgb& rhs) noexcept {
  return !(lhs == rhs);
}

std::string ramp_name(Ramp ramp) {
  switch (ramp) {
    case Ramp::Viridis:
      return "viridis";
    case Ramp::PastureGreen:
      return "pasture green";
  }
  return "unknown";
}

std::string tick_label_format(double low, double high, int labels) {
  const double step = (high - low) / static_cast<double>(std::max(labels - 1, 1));
  // A degenerate or reversed range has no step to read places off. Two places
  // is the least misleading fallback: it neither hides a fraction nor implies
  // precision that a single repeated label would not have anyway.
  if (!(step > 0.0)) {
    return "%.2f";
  }
  // One decimal place finer than the step. Six is the point past which a label
  // is wider than any legend that has to hold five of them.
  const int places = std::clamp(static_cast<int>(std::ceil(-std::log10(step))) + 1, 0, 6);
  return "%." + std::to_string(places) + "f";
}

ColourScale::ColourScale(Ramp ramp, double minimum, double maximum)
    : ramp_(ramp), minimum_(minimum), maximum_(maximum) {
  if (!(maximum_ > minimum_)) {
    // An inverted or empty range would map every value to one end. Widening it
    // keeps the scale usable and visibly uninformative, which is honest.
    maximum_ = minimum_ + 1.0;
  }
}

Rgb ColourScale::colour_of(double value) const noexcept {
  const double position = std::clamp((value - minimum_) / (maximum_ - minimum_), 0.0, 1.0);
  switch (ramp_) {
    case Ramp::PastureGreen:
      return interpolate(kPastureGreen, position);
    case Ramp::Viridis:
    default:
      return interpolate(kViridis, position);
  }
}

std::vector<Rgb> ColourScale::lookup_table(std::size_t entries) const {
  std::vector<Rgb> table;
  if (entries == 0) {
    return table;
  }
  table.reserve(entries);
  if (entries == 1) {
    table.push_back(colour_of(minimum_));
    return table;
  }
  for (std::size_t i = 0; i < entries; ++i) {
    const double weight = static_cast<double>(i) / static_cast<double>(entries - 1);
    table.push_back(colour_of(minimum_ + (weight * (maximum_ - minimum_))));
  }
  return table;
}

std::pair<double, double> ColourScale::range_of(const core::Raster<double>& raster) {
  if (raster.empty()) {
    return {0.0, 1.0};
  }
  double lowest = raster.values().front();
  double highest = lowest;
  for (const double value : raster.values()) {
    lowest = std::min(lowest, value);
    highest = std::max(highest, value);
  }
  if (!(highest > lowest)) {
    return {lowest, lowest + 1.0};
  }
  return {lowest, highest};
}

}  // namespace paddock::viz
