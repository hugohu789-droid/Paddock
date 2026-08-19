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
#include <fstream>
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

/// The same plane with a single pixel raised sharply, at a known place.
///
/// A plane cannot tell the two sampling rules apart: the mean of a linear
/// surface over a symmetric footprint is exactly its value at the centre, so
/// nearest-neighbour and cell-mean agree to the last bit. One spike is the
/// smallest surface on which they must differ.
class TemporaryGeoTiffWithASpike {
 public:
  static constexpr std::size_t kSpikeCol = 12;
  static constexpr std::size_t kSpikeRow = 8;
  static constexpr double kSpikeM = 50.0;

  explicit TemporaryGeoTiffWithASpike(const std::string& name) {
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
    surface(kSpikeCol, kSpikeRow) += kSpikeM;
    write_geotiff(surface, path_);
    written_ = true;
  }

  ~TemporaryGeoTiffWithASpike() {
    std::error_code ignored;
    std::filesystem::remove(path_, ignored);
  }

  TemporaryGeoTiffWithASpike(const TemporaryGeoTiffWithASpike&) = delete;
  TemporaryGeoTiffWithASpike& operator=(const TemporaryGeoTiffWithASpike&) = delete;
  TemporaryGeoTiffWithASpike(TemporaryGeoTiffWithASpike&&) = delete;
  TemporaryGeoTiffWithASpike& operator=(TemporaryGeoTiffWithASpike&&) = delete;

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

// A cell is worth the ground it covers, not the pixel under its middle.
//
// Read at four times the pixel size, each output cell covers sixteen source
// pixels. The cell holding the spike must carry a sixteenth of it - the spike
// is real and averaging it away entirely would be losing measured ground - and
// the cell centre must NOT be able to swallow it whole or miss it entirely,
// which is what sampling one pixel does depending on where the centre lands.
TEST(GeoTiffElevationTest, ACellIsTheMeanOfTheGroundItCovers) {
  const TemporaryGeoTiffWithASpike fixture("paddock_geotiff_spike");
  ASSERT_TRUE(fixture.written());
  const GeoTiffElevationSource source(fixture.path());

  const double cell_size = kPixelSizeM * 4.0;
  const core::Raster<double> elevation = source.fetch(
      box(kOriginEasting, kOriginNorthing - (static_cast<double>(kRows) * kPixelSizeM),
          kOriginEasting + (static_cast<double>(kCols) * kPixelSizeM), kOriginNorthing),
      cell_size);

  const std::size_t cell_col = TemporaryGeoTiffWithASpike::kSpikeCol / 4;
  const std::size_t cell_row = TemporaryGeoTiffWithASpike::kSpikeRow / 4;

  // What the plane alone would give at this cell: the mean of a linear surface
  // over the cell is its value at the cell centre.
  const double centre_easting =
      kOriginEasting + ((static_cast<double>(cell_col) + 0.5) * cell_size);
  const double centre_northing =
      kOriginNorthing - ((static_cast<double>(cell_row) + 0.5) * cell_size);
  const double without_spike = plane_elevation(centre_easting, centre_northing);

  EXPECT_NEAR(elevation(cell_col, cell_row),
              without_spike + (TemporaryGeoTiffWithASpike::kSpikeM / 16.0), 1e-9)
      << "one raised pixel in sixteen should raise the cell by a sixteenth of it";

  // And every cell that does not contain the spike is the plane exactly, so the
  // averaging has not smeared it into the neighbours.
  for (std::size_t row = 0; row < elevation.rows(); ++row) {
    for (std::size_t col = 0; col < elevation.cols(); ++col) {
      if (col == cell_col && row == cell_row) {
        continue;
      }
      const double easting = kOriginEasting + ((static_cast<double>(col) + 0.5) * cell_size);
      const double northing = kOriginNorthing - ((static_cast<double>(row) + 0.5) * cell_size);
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
  EXPECT_NE(status.message.find("nz-elevation-snapshot.py"), std::string::npos) << status.message;
}

// The licence is not decoration: LINZ data is CC BY 4.0 and attribution is a
// condition of using it, so it travels with the source.
//
// What it must NOT do is name a licensor, because this class does not know one.
// LINZ elevation is open data whose licensor differs by capture - Environment
// Canterbury for the Canterbury LiDAR - and the file that knows is the
// provenance written beside the snapshot.
TEST(GeoTiffElevationTest, TheDescriptionCarriesTheLicenceAndPointsAtTheProvenance) {
  const GeoTiffElevationSource source("anything.tif");

  const core::SourceDescription description = source.describe();

  EXPECT_NE(description.licence.find("Creative Commons Attribution 4.0"), std::string::npos);
  EXPECT_NE(description.licence.find("provenance.json"), std::string::npos)
      << "the description should say where the capture's own licensor is recorded";
}

// A file that is missing and a file that cannot be decoded both fail to open,
// and they need different answers. Telling somebody to fetch a snapshot they
// already have sends them the wrong way, which is exactly what happened when
// the first LINZ tile turned out to be LERC compressed.
TEST(GeoTiffElevationTest, AFileThatIsThereAndUnreadableIsNotReportedAsMissing) {
  const std::filesystem::path path =
      std::filesystem::temp_directory_path() / "paddock-not-a-geotiff.tif";
  {
    std::ofstream file(path, std::ios::binary);
    file << "this is not a GeoTIFF";
  }

  const GeoTiffElevationSource source(path.string());
  const core::ConnectionStatus status = source.test_connection();
  std::filesystem::remove(path);

  EXPECT_FALSE(status.ok);
  EXPECT_EQ(status.message.find("is not there"), std::string::npos)
      << "a file that exists was reported as missing: " << status.message;
  EXPECT_NE(status.message.find("will not open"), std::string::npos) << status.message;
  EXPECT_NE(status.message.find("LERC"), std::string::npos)
      << "the message should name the compression that caused this in practice";
}

}  // namespace
}  // namespace paddock::gis
