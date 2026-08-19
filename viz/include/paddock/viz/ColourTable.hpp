// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#pragma once

#include <vtkLookupTable.h>

#include <paddock/viz/ColourScale.hpp>

namespace paddock::viz {

/// Fills a VTK lookup table from a ColourScale.
///
/// Shared because the flat map and the terrain view must colour the same value the
/// same way. Two copies of this loop would be two colour schemes that agreed
/// until one of them was edited, and a reader comparing the two views would be
/// comparing paint rather than pasture.
///
/// `entries` is the resolution of the ramp. 256 is more than a display can
/// distinguish and small enough to rebuild every frame without noticing.
void fill_lookup_table(vtkLookupTable* table, const ColourScale& scale, int entries = 256);

}  // namespace paddock::viz
