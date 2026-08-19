// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#pragma once

#include <array>
#include <cstddef>
#include <functional>
#include <vector>
#include <vtkPolyData.h>

#include <paddock/core/AnimalEnergy.hpp>
#include <paddock/core/Geometry.hpp>

namespace paddock::viz {

/// Stock on the map: where they are, what they are, and how many.
///
/// One marker per paddock a mob occupies rather than one per mob. Under
/// rotation that is a single mark; under set stocking it is a mark on every
/// paddock, because that is what set stocking is - the whole farm at once - and
/// a single mark in the middle would draw it as a mob standing in a gateway.
struct MobMarker {
  core::Point2D at;
  core::AnimalKind kind = core::AnimalKind::Other;

  /// The whole mob's head count, not this paddock's share. Nothing in this
  /// model says how a set stocked mob distributes itself over the ground it has
  /// the run of, so dividing the number by the paddocks would be inventing a
  /// distribution to make a label look precise.
  int head = 0;
};

/// The colour a kind is drawn in, as red, green and blue from 0 to 1.
[[nodiscard]] std::array<double, 3> colour_of(core::AnimalKind kind);

/// Builds the markers of one kind into `into`, as filled polygons lying in the
/// ground plane.
///
/// **Shape carries the species and colour only reinforces it.** A reader who
/// cannot separate the two colours can still separate a circle from a square,
/// which a map of two mobs has to allow.
///
/// `size_m` is the marker's width on the ground, so it scales with the farm
/// rather than with the window: zooming in makes a mob larger, as a paddock is.
///
/// `height` gives the ground height at a point, for a scene that has terrain.
/// A flat scene passes one that returns a constant.
void build_mob_markers(const std::vector<MobMarker>& markers, core::AnimalKind kind, double size_m,
                       const std::function<double(core::Point2D)>& height, vtkPolyData* into);

/// Every kind a marker can be, so a caller can build one layer per kind without
/// naming them and getting the list wrong when one is added.
[[nodiscard]] const std::vector<core::AnimalKind>& marker_kinds();

}  // namespace paddock::viz
