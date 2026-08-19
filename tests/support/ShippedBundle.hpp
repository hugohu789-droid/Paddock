// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#ifndef PADDOCK_TESTS_SUPPORT_SHIPPED_BUNDLE_HPP
#define PADDOCK_TESTS_SUPPORT_SHIPPED_BUNDLE_HPP

#include <string>

#include <paddock/config/ScenarioConfig.hpp>

namespace paddock::tests {

/// Load a shipped bundle and flatten its ground.
///
/// **Why the tests take the ground away.** Every shipped bundle now names a
/// LiDAR snapshot, and reading a GeoTIFF needs GDAL. These suites deliberately
/// do not link it - config must not depend on gis, and a fast suite that needs
/// a geospatial stack installed is a suite that stops being run. Asked for
/// ground it cannot read, a bundle refuses rather than quietly running flat,
/// which is the right answer and is not one a management test can act on.
///
/// **Why flattening does not weaken what they check.** These are tests of the
/// farmer's decisions - when feed is bought, when a mob is moved, whether the
/// budgets close - and they were written against flat ground, because the
/// bundles were flat when they were written. Flattening here keeps every
/// pinned number measuring what it was pinned to measure. Re-baselining them
/// against real terrain instead would silently revalidate the model against
/// numbers nobody had checked.
///
/// Terrain reaches the model through TerrainReachesTheModelTest, which builds
/// its own surfaces and does not need a survey to do it.
[[nodiscard]] inline config::ScenarioBundle load_on_flat_ground(const std::string& directory) {
  config::ScenarioBundle bundle = config::load_scenario(directory);
  bundle.terrain = config::TerrainSpec{};
  return bundle;
}

}  // namespace paddock::tests

#endif  // PADDOCK_TESTS_SUPPORT_SHIPPED_BUNDLE_HPP
