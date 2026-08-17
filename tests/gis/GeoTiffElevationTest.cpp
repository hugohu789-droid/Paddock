// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

// Reading a GeoTIFF back into core's Raster<double>.
//
// The fixtures are written by the tests themselves, with GDAL, and deleted
// afterwards. That keeps binary files out of the repository - CLAUDE.md forbids
// committing datasets - and makes each assertion a round trip: a value written
// at a known easting and northing has to come back at the same easting and
// northing. A committed .tif would only ever prove that the reader agrees with
// whatever produced it, which nobody could inspect.

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include <paddock/gis/Environment.hpp>
#include <paddock/gis/GeoTiffElevation.hpp>
#include <paddock/gis/GeoTiffWriter.hpp>

namespace paddock::gis {
namespace {

constexpr double kOriginEasting = 1570000.0;
constexpr double kOriginNorthing = 5180000.0;  ///< North-west corner
constexpr double kPixelSizeM = 10.0;
constexpr std::size_t kCols = 40;
constexpr std::size_t kRows = 32;

/// Elevation of the test surface: a plane, so any sample can be checked by
/// arithmetic rather than against a table.
double plane_elevation(double easting, double northing) {
  return 100.0 + (0.05 * (easting - kOriginEasting)) - (0.10 * (kOriginNorthing - northing));
}

/// Writes a north-up GeoTIFF of that plane through this project's own writer,
/// and removes it afterwards.
///
/// Using write_geotiff rather than GDAL directly is not only convenience: the
/// test target links Paddock::gis, which links GDAL PRIVATE, so no GDAL header
/// is reachable from here at all. That is the boundary ADR 0011 set up, working
/// as intended.
class TemporaryGeoTiff {
 public:
  explicit TemporaryGeoTiff(const std::string& name) {
    path_ = (std::filesystem::temp_directory_path() / (name + ".tif")).string();

    core::GeoTransform transform;
    transform.origin_easting = kOriginEasting;
    transform.origin_northing = kOriginNorthing;
    transform.cell_size = kPixelSizeM;

    core::Raster<double> surface(kCols, kRows, transform);
    for (std::size_t row = 0; row < surface.rows(); ++row) {
      for (std::size_t col = 0; col < surface.cols(); ++col) {
        const double easting = kOriginEasting + ((static_cast<double>(col) + 0.5) * kPixelSizeM);
        const double northing = kOriginNorthing - ((static_cast<double>(row) + 0.5) * kPixelSizeM);
        surface(col, row) = plane_elevation(easting, northing);
      }
    }
    write_geotiff(surface, path_);
    written_ = true;
  }

  ~TemporaryGeoTiff() {
    // Setting PADDOCK_KEEP_TEST_FIXTURES leaves the file behind, so that what
    // this project wrote can be inspected with gdalinfo or opened in QGIS.
    // A round trip through our own writer and our own reader would agree with
    // itself even if both were wrong about row order; an outside tool is the
    // only thing that settles it.
    if (std::getenv("PADDOCK_KEEP_TEST_FIXTURES") != nullptr) {
      return;
    }
    std::error_code ignored;
    std::filesystem::remove(path_, ignored);
  }

  TemporaryGeoTiff(const TemporaryGeoTiff&) = delete;
  TemporaryGeoTiff& operator=(const TemporaryGeoTiff&) = delete;
  TemporaryGeoTiff(TemporaryGeoTiff&&) = delete;
  TemporaryGeoTiff& operator=(TemporaryGeoTiff&&) = delete;

  [[nodiscard]] const std::string& path() const noexcept { return path_; }

  [[nodiscard]] bool written() const noexcept { return written_; }

 private:
  std::string path_;
  bool written_ = false;
};

core::BoundingBox box(double west, double south, double east, double north) {
  core::BoundingBox area = core::BoundingBox::empty();
  area.expand_to_include(core::Point2D{west, south});
  area.expand_to_include(core::Point2D{east, north});
  return area;
}

TEST(GeoTiffElevationTest, ReportsWhatItCoversAndThatItIsReadable) {
  const TemporaryGeoTiff fixture("paddock_geotiff_coverage");
  ASSERT_TRUE(fixture.written());
  const GeoTiffElevationSource source(fixture.path());

  const core::ConnectionStatus status = source.test_connection();
  EXPECT_TRUE(status.ok) << status.message;

  const core::BoundingBox area = source.coverage();
  EXPECT_DOUBLE_EQ(area.min_easting, kOriginEasting);
  EXPECT_DOUBLE_EQ(area.max_easting, kOriginEasting + (static_cast<double>(kCols) * kPixelSizeM));
  EXPECT_DOUBLE_EQ(area.max_northing, kOriginNorthing);
  EXPECT_DOUBLE_EQ(area.min_northing, kOriginNorthing - (static_cast<double>(kRows) * kPixelSizeM));
}

// The round trip that matters: a height written at a known position comes back
// at that position. An easting/northing transposition, or a row flip, changes
// which value lands where and nothing else - so this is what catches it.
TEST(GeoTiffElevationTest, ValuesComeBackAtThePositionTheyWereWrittenAt) {
  const TemporaryGeoTiff fixture("paddock_geotiff_roundtrip");
  ASSERT_TRUE(fixture.written());
  const GeoTiffElevationSource source(fixture.path());

  const core::Raster<double> elevation = source.fetch(
      box(kOriginEasting, kOriginNorthing - (static_cast<double>(kRows) * kPixelSizeM),
          kOriginEasting + (static_cast<double>(kCols) * kPixelSizeM), kOriginNorthing),
      kPixelSizeM);

  ASSERT_EQ(elevation.cols(), kCols);
  ASSERT_EQ(elevation.rows(), kRows);

  for (std::size_t row = 0; row < elevation.rows(); ++row) {
    for (std::size_t col = 0; col < elevation.cols(); ++col) {
      const double easting = kOriginEasting + ((static_cast<double>(col) + 0.5) * kPixelSizeM);
      const double northing = kOriginNorthing - ((static_cast<double>(row) + 0.5) * kPixelSizeM);
      ASSERT_NEAR(elevation(col, row), plane_elevation(easting, northing), 1e-9)
          << "at column " << col << ", row " << row;
    }
  }
}

// Row 0 is the northernmost in a Paddock raster and in a north-up GeoTIFF
// alike. The test surface falls to the south and rises to the east, so the
// north-west corner is high and the south-west corner is low - an upside-down
// read swaps them and produces a perfectly plausible raster with the farm's
// slope reversed.
TEST(GeoTiffElevationTest, TheFirstRowIsTheNorthernmost) {
  const TemporaryGeoTiff fixture("paddock_geotiff_orientation");
  ASSERT_TRUE(fixture.written());
  const GeoTiffElevationSource source(fixture.path());

  const core::Raster<double> elevation = source.fetch(
      box(kOriginEasting, kOriginNorthing - (static_cast<double>(kRows) * kPixelSizeM),
          kOriginEasting + (static_cast<double>(kCols) * kPixelSizeM), kOriginNorthing),
      kPixelSizeM);

  EXPECT_GT(elevation(0, 0), elevation(0, elevation.rows() - 1)) << "north should be higher";
  EXPECT_LT(elevation(0, 0), elevation(elevation.cols() - 1, 0)) << "east should be higher";
}

// A window inside the file has to agree with the same cells of the whole, the
// same property the synthetic source is held to.
TEST(GeoTiffElevationTest, AWindowAgreesWithTheWholeFile) {
  const TemporaryGeoTiff fixture("paddock_geotiff_window");
  ASSERT_TRUE(fixture.written());
  const GeoTiffElevationSource source(fixture.path());

  const core::Raster<double> whole = source.fetch(
      box(kOriginEasting, kOriginNorthing - (static_cast<double>(kRows) * kPixelSizeM),
          kOriginEasting + (static_cast<double>(kCols) * kPixelSizeM), kOriginNorthing),
      kPixelSizeM);
  const core::Raster<double> part =
      source.fetch(box(kOriginEasting + 100.0, kOriginNorthing - 200.0, kOriginEasting + 200.0,
                       kOriginNorthing - 100.0),
                   kPixelSizeM);

  ASSERT_EQ(part.cols(), 10U);
  ASSERT_EQ(part.rows(), 10U);
  for (std::size_t row = 0; row < part.rows(); ++row) {
    for (std::size_t col = 0; col < part.cols(); ++col) {
      ASSERT_DOUBLE_EQ(part(col, row), whole(col + 10, row + 10))
          << "window cell (" << col << ", " << row << ")";
    }
  }
}

// Asking for ground the file does not have is an error, not a quietly short
// raster: outside the DEM every cell would read as flat, which is a slope of
// zero and a growth modifier of one.
TEST(GeoTiffElevationTest, AreaOutsideTheFileIsRefused) {
  const TemporaryGeoTiff fixture("paddock_geotiff_outside");
  ASSERT_TRUE(fixture.written());
  const GeoTiffElevationSource source(fixture.path());

  EXPECT_THROW(static_cast<void>(source.fetch(box(kOriginEasting - 500.0, kOriginNorthing - 100.0,
                                                  kOriginEasting + 100.0, kOriginNorthing),
                                              kPixelSizeM)),
               std::out_of_range);
}

// A missing snapshot is the everyday case - they are gitignored - so the
// message has to name the file and say how to get one.
TEST(GeoTiffElevationTest, AMissingFileIsReportedWithSomethingToDoAboutIt) {
  const GeoTiffElevationSource source("no/such/dem.tif");

  const core::ConnectionStatus status = source.test_connection();

  EXPECT_FALSE(status.ok);
  EXPECT_NE(status.message.find("no/such/dem.tif"), std::string::npos) << status.message;
  EXPECT_NE(status.message.find("linz-snapshot.py"), std::string::npos) << status.message;
}

// The licence is not decoration: LINZ data is CC BY 4.0 and attribution is a
// condition of using it, so it travels with the source.
TEST(GeoTiffElevationTest, TheDescriptionCarriesTheLinzAttribution) {
  const GeoTiffElevationSource source("anything.tif");

  const core::SourceDescription description = source.describe();

  EXPECT_NE(description.licence.find("LINZ Data Service"), std::string::npos);
  EXPECT_NE(description.licence.find("Creative Commons Attribution 4.0"), std::string::npos);
}

}  // namespace
}  // namespace paddock::gis
