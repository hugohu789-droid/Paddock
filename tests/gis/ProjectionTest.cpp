// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

// NZGD2000 <-> NZTM2000, against coordinates this project did not compute.
//
// A round-trip test on its own proves almost nothing about a projection: the
// forward and inverse transforms are inverses of each other whatever the
// parameters are, so a build with the central meridian set to 175 degrees
// instead of 173 would round-trip to the nanometre and put every farm in the
// wrong place. Two kinds of assertion are needed, and both are here:
//
//   * agreement with independently published values, which catches wrong
//     parameters and a swapped axis order;
//   * round-trip closure, which catches loss of precision.
//
// CLAUDE.md asks for the second at known control points, under 1 mm.

#include <gtest/gtest.h>

#include <array>
#include <cmath>

#include <paddock/gis/Projection.hpp>

namespace paddock::gis {
namespace {

/// A position and the NZTM2000 coordinates LINZ says it has.
struct ControlPoint {
  const char* place;
  double longitude_degrees;
  double latitude_degrees;
  double easting;
  double northing;
};

/// Reference values from LINZ's own coordinate conversion service, the same
/// engine behind the Concord converter, retrieved 2026-08-17. POST to
///
///   https://www.geodesy.linz.govt.nz/api/conversions/v1/convert-to
///       ?crs=LINZ:NZTM&coordinateOrder=north/east
///
/// with Content-Type application/json and the body
///
///   {"crs": "LINZ:NZGD2000",
///    "coordinateOrder": ["east", "north", "up"],
///    "coordinates": [[173.0, -41.0, 0.0]]}
///
/// (no line-continuation backslashes in this comment: a backslash at the end of
/// a comment line splices the next line into it, and GCC rejects the result
/// under -Werror=comment.)
///
/// The service returns northing first, because that is the axis order
/// EPSG:2193 declares; the columns below are transposed into the order this
/// project reads them in.
///
/// The points span the country, from Cape Reinga to Bluff, so that a parameter
/// error shows up as a growing discrepancy away from the central meridian
/// rather than hiding in one region.
constexpr std::array<ControlPoint, 8> kControlPoints = {{
    {"Cape Reinga area", 172.681400, -34.428900, 1570726.6223, 6190240.7606},
    {"Ruakura, Hamilton", 175.316700, -37.783300, 1804011.1268, 5815700.0870},
    {"Palmerston North", 175.611000, -40.384000, 1821633.1767, 5526347.9734},
    {"Wellington", 174.776000, -41.288900, 1748713.3532, 5427650.3685},
    {"Canterbury Plains", 172.470000, -43.641000, 1557252.3158, 5167862.7895},
    {"Upper Clutha", 169.150000, -44.700000, 1294979.1948, 5043161.0651},
    {"Bluff", 168.350000, -46.600000, 1243873.0491, 4828773.6953},
    {"On the central meridian", 173.000000, -41.000000, 1600000.0000, 5461242.9380},
}};

/// CLAUDE.md's tolerance for a coordinate transform.
constexpr double kOneMillimetre = 0.001;

TEST(ProjectionTest, MatchesLinzPublishedCoordinates) {
  const Projection projection;

  for (const ControlPoint& point : kControlPoints) {
    const Nztm computed =
        projection.to_nztm(Geographic{point.longitude_degrees, point.latitude_degrees});

    EXPECT_NEAR(computed.easting, point.easting, kOneMillimetre) << point.place << " easting";
    EXPECT_NEAR(computed.northing, point.northing, kOneMillimetre) << point.place << " northing";
  }
}

// The false easting, straight out of LINZ's definition of the projection: on
// the central meridian of 173 degrees east, the easting is exactly 1 600 000 m.
// Nothing else in this file fails so loudly if the central meridian is wrong.
TEST(ProjectionTest, TheCentralMeridianSitsOnTheFalseEasting) {
  const Projection projection;

  const Nztm on_meridian = projection.to_nztm(Geographic{173.0, -41.0});

  EXPECT_NEAR(on_meridian.easting, 1600000.0, kOneMillimetre);
}

// A swapped axis order is the failure this guards. EPSG:2193 declares
// (northing, easting), so a build that does not normalise returns them the
// other way round - and both are seven-digit numbers, so nothing looks wrong.
// Every New Zealand northing is larger than every New Zealand easting, which
// makes the mistake detectable.
TEST(ProjectionTest, EastingAndNorthingAreNotSwapped) {
  const Projection projection;

  for (const ControlPoint& point : kControlPoints) {
    const Nztm computed =
        projection.to_nztm(Geographic{point.longitude_degrees, point.latitude_degrees});

    EXPECT_GT(computed.northing, computed.easting) << point.place;
    EXPECT_GT(computed.northing, 4.0e6) << point.place;
    EXPECT_LT(computed.easting, 2.5e6) << point.place;
  }
}

// The requirement from CLAUDE.md, measured in metres because the round trip
// starts and ends in metres. Going the other way round would leave the error in
// degrees, where a tolerance has to be converted before it means anything.
TEST(ProjectionTest, RoundTripClosesToUnderOneMillimetre) {
  const Projection projection;

  for (const ControlPoint& point : kControlPoints) {
    const Nztm original{point.easting, point.northing};

    const Geographic geographic = projection.to_geographic(original);
    const Nztm returned = projection.to_nztm(geographic);

    const double drift =
        std::hypot(returned.easting - original.easting, returned.northing - original.northing);
    EXPECT_LT(drift, kOneMillimetre) << point.place << ": round trip drifted " << drift << " m";
  }
}

// Transforming is not a one-shot operation - the map view will do it per
// vertex - so the same input has to give the same output every time. A stale
// PROJ context or a transform that accumulated state would show up here.
TEST(ProjectionTest, RepeatedTransformsAreIdentical) {
  const Projection projection;
  const Geographic point{kControlPoints[0].longitude_degrees, kControlPoints[0].latitude_degrees};

  const Nztm first = projection.to_nztm(point);
  for (int i = 0; i < 100; ++i) {
    const Nztm again = projection.to_nztm(point);
    ASSERT_DOUBLE_EQ(again.easting, first.easting) << "at repetition " << i;
    ASSERT_DOUBLE_EQ(again.northing, first.northing) << "at repetition " << i;
  }
}

}  // namespace
}  // namespace paddock::gis
