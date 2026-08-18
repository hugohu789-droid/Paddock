// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

// Farm descriptions: the format a boundary editor will read and write.
//
// The editor of task #22 does not exist. This suite is what stops that from
// mattering: the format is fixed, validated and tested now, so the editor,
// when it arrives, is a way of writing these files rather than a second place
// where a farm can be defined.
//
// What the assertions here are worth varies, and the comments say which:
// coordinate bounds and area arithmetic are things a reader can check, and the
// Massey figures are published by Massey.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include <paddock/config/ConfigError.hpp>
#include <paddock/config/FarmConfig.hpp>

namespace paddock::config {
namespace {

constexpr std::string_view kSyntheticFarm = R"(
[farm]
name = "example"
region = "Canterbury"

[location]
centre_easting = 1557252.0
centre_northing = 5167862.0
latitude_degrees = -43.641

[boundary]
kind = "synthetic"
extent_width_m = 1000.0
extent_height_m = 500.0
paddock_hectares = 2.5
)";

FarmDefinition parse(std::string_view text) {
  return parse_farm(text, "test.toml");
}

TEST(FarmConfigTest, ASyntheticFarmGeneratesPaddocksCoveringItsExtent) {
  const FarmDefinition farm = parse(kSyntheticFarm);

  EXPECT_EQ(farm.name, "example");
  EXPECT_EQ(farm.boundary_source, BoundarySource::Synthetic);
  // Verification, not a pin: 1000 m by 500 m is 500 000 m2, which is 50 ha.
  EXPECT_NEAR(farm.boundary_hectares(), 50.0, 1e-9);
  EXPECT_FALSE(farm.paddocks.empty() && farm.make_paddocks().empty());
}

// The extent is laid out around the centre the file gives, so that moving a
// farm does not also resize it. Checked by measuring the paddocks' own bounds.
TEST(FarmConfigTest, TheExtentIsCentredOnTheDeclaredCentre) {
  const FarmDefinition farm = parse(kSyntheticFarm);

  core::BoundingBox bounds = core::BoundingBox::empty();
  for (const core::Paddock& paddock : farm.make_paddocks()) {
    const core::BoundingBox one = paddock.boundary.bounds();
    bounds.expand_to_include(core::Point2D{one.min_easting, one.min_northing});
    bounds.expand_to_include(core::Point2D{one.max_easting, one.max_northing});
  }

  EXPECT_NEAR((bounds.min_easting + bounds.max_easting) / 2.0, 1557252.0, 1e-6);
  EXPECT_NEAR((bounds.min_northing + bounds.max_northing) / 2.0, 5167862.0, 1e-6);
  EXPECT_NEAR(bounds.width(), 1000.0, 1e-6);
  EXPECT_NEAR(bounds.height(), 500.0, 1e-6);
}

// Inline boundaries are the form the editor will write, so the format has to
// round-trip a hand-written polygon with the area it actually encloses.
TEST(FarmConfigTest, InlinePaddocksAreReadWithTheirGeometry) {
  const FarmDefinition farm = parse(R"(
[farm]
name = "inline_example"
region = "Waikato"

[location]
centre_easting = 1804011.0
centre_northing = 5815700.0
latitude_degrees = -37.7833

[boundary]
kind = "inline"

[[paddock]]
name = "North 1"
vertices = [[1804000.0, 5815700.0], [1804200.0, 5815700.0],
            [1804200.0, 5815600.0], [1804000.0, 5815600.0]]

[[paddock]]
name = "North 2"
vertices = [[1804200.0, 5815700.0], [1804400.0, 5815700.0],
            [1804400.0, 5815600.0], [1804200.0, 5815600.0]]
)");

  ASSERT_EQ(farm.paddocks.size(), 2U);
  EXPECT_EQ(farm.paddocks[0].name, "North 1");
  // 200 m by 100 m is 2 ha, twice over.
  EXPECT_NEAR(farm.paddocks[0].area_hectares(), 2.0, 1e-9);
  EXPECT_NEAR(farm.boundary_hectares(), 4.0, 1e-9);
}

// A GeoPackage farm parses but does not generate: reading one needs GDAL, and
// config/ does not link it. Failing loudly here is what keeps the dependency
// out of builds that do not want it.
TEST(FarmConfigTest, AGeoPackageFarmParsesButRefusesToGenerate) {
  const FarmDefinition farm = parse(R"(
[farm]
name = "from_linz"
region = "Southland"

[location]
centre_easting = 1243873.0
centre_northing = 4828773.0
latitude_degrees = -46.6

[boundary]
kind = "geopackage"
path = "data/snapshots/example-parcels.gpkg"
layer = "nz_primary_parcels"
sha256 = "0000000000000000000000000000000000000000000000000000000000000000"
)");

  EXPECT_EQ(farm.boundary_source, BoundarySource::GeoPackage);
  EXPECT_EQ(farm.boundary_layer, "nz_primary_parcels");
  EXPECT_THROW(static_cast<void>(farm.make_paddocks()), std::runtime_error);
}

// EPSG:2193 declares (northing, easting) and this project writes (easting,
// northing), so a transposed pair is the coordinate mistake most likely to
// happen. It has to be caught by name rather than by an empty raster later.
TEST(FarmConfigTest, TransposedCoordinatesAreRejected) {
  EXPECT_THROW(static_cast<void>(parse(R"(
[farm]
name = "transposed"
region = "Canterbury"

[location]
centre_easting = 5167862.0
centre_northing = 1557252.0
latitude_degrees = -43.641

[boundary]
kind = "synthetic"
extent_width_m = 1000.0
extent_height_m = 500.0
)")),
               ConfigError);
}

// A northern-hemisphere latitude is a dropped sign, and a farm at +43 would
// run a mirrored year: growth peaking in July rather than January.
TEST(FarmConfigTest, APositiveLatitudeIsRejected) {
  EXPECT_THROW(static_cast<void>(parse(R"(
[farm]
name = "wrong_hemisphere"
region = "Canterbury"

[location]
centre_easting = 1557252.0
centre_northing = 5167862.0
latitude_degrees = 43.641

[boundary]
kind = "synthetic"
extent_width_m = 1000.0
extent_height_m = 500.0
)")),
               ConfigError);
}

// The flag exists to say "this came from somewhere". Letting it be set without
// naming the somewhere would make it decoration.
TEST(FarmConfigTest, ClaimingAVerifiedLocationRequiresSayingWhereItCameFrom) {
  const std::string body = R"(
[farm]
name = "claims_a_survey"
region = "Canterbury"

[location]
centre_easting = 1557252.0
centre_northing = 5167862.0
latitude_degrees = -43.641
location_verified = true
)";

  EXPECT_THROW(static_cast<void>(parse(body)), ConfigError);

  const FarmDefinition with_source = parse(body + "source = \"LINZ survey\"\n" + R"(
[boundary]
kind = "synthetic"
extent_width_m = 1000.0
extent_height_m = 500.0
)");
  EXPECT_TRUE(with_source.location.location_verified);
}

// Paddocks listed under a farm that does not read them would be silently
// ignored - the reader would see a described farm and get a generated one.
TEST(FarmConfigTest, PaddocksUnderANonInlineFarmAreRejected) {
  EXPECT_THROW(static_cast<void>(parse(std::string(kSyntheticFarm) + R"(
[[paddock]]
name = "ignored"
vertices = [[1557000.0, 5167000.0], [1557100.0, 5167000.0], [1557100.0, 5167100.0]]
)")),
               ConfigError);
}

TEST(FarmConfigTest, AMistypedKeyIsRejectedRatherThanIgnored) {
  EXPECT_THROW(static_cast<void>(parse(R"(
[farm]
name = "typo"
region = "Canterbury"

[location]
centre_easting = 1557252.0
centre_northing = 5167862.0
latitude_degrees = -43.641

[boundary]
kind = "synthetic"
extent_width_m = 1000.0
extent_hieght_m = 500.0
)")),
               ConfigError);
}

TEST(FarmConfigTest, AnUnknownBoundaryKindIsRejected) {
  EXPECT_THROW(static_cast<void>(parse(R"(
[farm]
name = "unknown_kind"
region = "Canterbury"

[location]
centre_easting = 1557252.0
centre_northing = 5167862.0
latitude_degrees = -43.641

[boundary]
kind = "shapefile"
)")),
               ConfigError);
}

TEST(FarmConfigTest, ADegeneratePaddockIsRejected) {
  EXPECT_THROW(static_cast<void>(parse(R"(
[farm]
name = "degenerate"
region = "Waikato"

[location]
centre_easting = 1804011.0
centre_northing = 5815700.0
latitude_degrees = -37.7833

[boundary]
kind = "inline"

[[paddock]]
name = "A line, not a paddock"
vertices = [[1804000.0, 5815700.0], [1804200.0, 5815700.0]]
)")),
               ConfigError);
}

}  // namespace
}  // namespace paddock::config
