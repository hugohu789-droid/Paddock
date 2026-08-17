// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

// Reading paddock boundaries back out of a GeoPackage.
//
// Fixtures are written by this project's own writer and removed afterwards, so
// nothing binary enters the repository. Set PADDOCK_KEEP_TEST_FIXTURES to leave
// the file behind and open it in QGIS - a round trip through our writer and our
// reader agrees with itself even when both are wrong, and an outside tool is
// what settles that.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include <paddock/gis/GeoPackageParcels.hpp>
#include <paddock/gis/GeoPackageWriter.hpp>

namespace paddock::gis {
namespace {

constexpr double kWest = 1570000.0;
constexpr double kSouth = 5179000.0;
constexpr double kPaddockWidth = 200.0;
constexpr double kPaddockHeight = 125.0;

core::BoundingBox box(double west, double south, double east, double north) {
  core::BoundingBox area = core::BoundingBox::empty();
  area.expand_to_include(core::Point2D{west, south});
  area.expand_to_include(core::Point2D{east, north});
  return area;
}

/// Four named paddocks in a row, each 200 m by 125 m - 2.5 ha, the size Massey's
/// Dairy 4 sits in the middle of.
std::vector<core::Paddock> four_paddocks() {
  std::vector<core::Paddock> paddocks;
  for (int i = 0; i < 4; ++i) {
    const core::Point2D corner{kWest + (i * kPaddockWidth), kSouth};
    paddocks.push_back(
        core::Paddock{"Front " + std::to_string(i + 1),
                      core::Polygon::rectangle(corner, kPaddockWidth, kPaddockHeight)});
  }
  return paddocks;
}

class TemporaryGeoPackage {
 public:
  explicit TemporaryGeoPackage(const std::string& name) {
    path_ = (std::filesystem::temp_directory_path() / (name + ".gpkg")).string();
    write_geopackage(four_paddocks(), path_);
  }

  ~TemporaryGeoPackage() {
    if (std::getenv("PADDOCK_KEEP_TEST_FIXTURES") != nullptr) {
      return;
    }
    std::error_code ignored;
    std::filesystem::remove(path_, ignored);
  }

  TemporaryGeoPackage(const TemporaryGeoPackage&) = delete;
  TemporaryGeoPackage& operator=(const TemporaryGeoPackage&) = delete;
  TemporaryGeoPackage(TemporaryGeoPackage&&) = delete;
  TemporaryGeoPackage& operator=(TemporaryGeoPackage&&) = delete;

  [[nodiscard]] const std::string& path() const noexcept { return path_; }

 private:
  std::string path_;
};

TEST(GeoPackageParcelsTest, ReportsHowManyFeaturesItHolds) {
  const TemporaryGeoPackage fixture("paddock_parcels_count");
  const GeoPackageParcelSource source(fixture.path());

  const core::ConnectionStatus status = source.test_connection();

  EXPECT_TRUE(status.ok) << status.message;
  EXPECT_NE(status.message.find('4'), std::string::npos) << status.message;
}

// Shape, position and area all have to survive the round trip. Area is the
// telling one: it is computed from the vertices, so a dropped or duplicated
// closing vertex, or a transposed pair of coordinates, changes it.
TEST(GeoPackageParcelsTest, PaddocksComeBackWithTheirShapeAndPlace) {
  const TemporaryGeoPackage fixture("paddock_parcels_roundtrip");
  const GeoPackageParcelSource source(fixture.path(), {}, "name");

  const std::vector<core::Paddock> paddocks =
      source.fetch(box(kWest - 10.0, kSouth - 10.0, kWest + 1000.0, kSouth + 500.0));

  ASSERT_EQ(paddocks.size(), 4U);
  for (const core::Paddock& paddock : paddocks) {
    EXPECT_EQ(paddock.boundary.vertex_count(), 4U) << paddock.name;
    EXPECT_NEAR(paddock.boundary.area(), kPaddockWidth * kPaddockHeight, 1e-6) << paddock.name;
    EXPECT_NEAR(paddock.area_hectares(), 2.5, 1e-9) << paddock.name;

    const core::BoundingBox bounds = paddock.boundary.bounds();
    EXPECT_GE(bounds.min_easting, kWest - 1e-6);
    EXPECT_NEAR(bounds.min_northing, kSouth, 1e-6);
  }
}

// The name field is what a farmer calls the paddock, and it is the difference
// between a report that says "Front 3" and one that says "feature 3".
TEST(GeoPackageParcelsTest, NamesComeFromTheFieldAskedFor) {
  const TemporaryGeoPackage fixture("paddock_parcels_names");
  const GeoPackageParcelSource source(fixture.path(), {}, "name");

  std::vector<core::Paddock> paddocks =
      source.fetch(box(kWest, kSouth, kWest + 1000.0, kSouth + 500.0));

  ASSERT_EQ(paddocks.size(), 4U);
  std::vector<std::string> names;
  names.reserve(paddocks.size());
  for (const core::Paddock& paddock : paddocks) {
    names.push_back(paddock.name);
  }
  std::sort(names.begin(), names.end());
  EXPECT_EQ(names, (std::vector<std::string>{"Front 1", "Front 2", "Front 3", "Front 4"}));
}

// Without a name field, paddocks are numbered rather than nameless. An empty
// name would print as a blank column in every report that lists them.
TEST(GeoPackageParcelsTest, WithoutANameFieldPaddocksAreNumbered) {
  const TemporaryGeoPackage fixture("paddock_parcels_unnamed");
  const GeoPackageParcelSource source(fixture.path());

  const std::vector<core::Paddock> paddocks =
      source.fetch(box(kWest, kSouth, kWest + 1000.0, kSouth + 500.0));

  ASSERT_EQ(paddocks.size(), 4U);
  for (const core::Paddock& paddock : paddocks) {
    EXPECT_FALSE(paddock.name.empty());
    EXPECT_EQ(paddock.name.rfind("Paddock ", 0), 0U) << paddock.name;
  }
}

// The spatial filter is the reason this scales: a farm-sized window out of a
// regional parcel file must not walk the whole country.
TEST(GeoPackageParcelsTest, OnlyPaddocksTouchingTheAreaComeBack) {
  const TemporaryGeoPackage fixture("paddock_parcels_filter");
  const GeoPackageParcelSource source(fixture.path(), {}, "name");

  // A window over the first paddock only.
  const std::vector<core::Paddock> paddocks =
      source.fetch(box(kWest + 10.0, kSouth + 10.0, kWest + 100.0, kSouth + 100.0));

  ASSERT_EQ(paddocks.size(), 1U);
  EXPECT_EQ(paddocks.front().name, "Front 1");
}

// An area with no paddocks in it is a question about the scenario, not a
// failure of the file, so it comes back empty rather than throwing.
TEST(GeoPackageParcelsTest, AnAreaWithNoPaddocksIsEmptyRatherThanAnError) {
  const TemporaryGeoPackage fixture("paddock_parcels_elsewhere");
  const GeoPackageParcelSource source(fixture.path());

  const std::vector<core::Paddock> paddocks =
      source.fetch(box(1600000.0, 5200000.0, 1600100.0, 5200100.0));

  EXPECT_TRUE(paddocks.empty());
}

TEST(GeoPackageParcelsTest, AMissingFileIsReportedWithSomethingToDoAboutIt) {
  const GeoPackageParcelSource source("no/such/parcels.gpkg");

  const core::ConnectionStatus status = source.test_connection();

  EXPECT_FALSE(status.ok);
  EXPECT_NE(status.message.find("no/such/parcels.gpkg"), std::string::npos) << status.message;
  EXPECT_NE(status.message.find("linz-snapshot.py"), std::string::npos) << status.message;
}

TEST(GeoPackageParcelsTest, AMissingLayerNamesTheLayerItLookedFor) {
  const TemporaryGeoPackage fixture("paddock_parcels_layer");
  const GeoPackageParcelSource source(fixture.path(), "not_a_layer");

  const core::ConnectionStatus status = source.test_connection();

  EXPECT_FALSE(status.ok);
  EXPECT_NE(status.message.find("not_a_layer"), std::string::npos) << status.message;
}

TEST(GeoPackageParcelsTest, TheDescriptionCarriesTheLinzAttribution) {
  const GeoPackageParcelSource source("anything.gpkg");

  const core::SourceDescription description = source.describe();

  EXPECT_NE(description.licence.find("LINZ Data Service"), std::string::npos);
  EXPECT_NE(description.licence.find("Creative Commons Attribution 4.0"), std::string::npos);
}

}  // namespace
}  // namespace paddock::gis
