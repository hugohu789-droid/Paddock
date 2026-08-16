#include <gtest/gtest.h>

#include <cstddef>
#include <optional>
#include <stdexcept>
#include <vector>

#include <paddock/core/Geometry.hpp>
#include <paddock/core/Raster.hpp>

namespace paddock::core {
namespace {

// A 200 ha block at 10 m resolution is the working scale: 20,000 cells, cheap
// enough to step daily.
constexpr std::size_t kCols = 50;
constexpr std::size_t kRows = 40;
constexpr double kCellSize = 10.0;

GeoTransform test_transform() {
  GeoTransform transform;
  transform.origin_easting = 1570000.0;
  transform.origin_northing = 5180000.0;  // north-west corner
  transform.cell_size = kCellSize;
  return transform;
}

Raster<double> test_raster(double fill = 0.0) {
  return {kCols, kRows, test_transform(), fill};
}

TEST(RasterTest, ShapeAndAreaFollowTheTransform) {
  const Raster<double> pasture = test_raster(1500.0);

  EXPECT_EQ(pasture.cols(), kCols);
  EXPECT_EQ(pasture.rows(), kRows);
  EXPECT_EQ(pasture.size(), kCols * kRows);
  EXPECT_DOUBLE_EQ(pasture.cell_area(), 100.0);
  EXPECT_DOUBLE_EQ(pasture.area_hectares(), 20.0);
  EXPECT_EQ(pasture.transform().epsg, kNztm2000Epsg);
}

TEST(RasterTest, RejectsANonPositiveCellSize) {
  GeoTransform broken = test_transform();
  broken.cell_size = 0.0;

  EXPECT_THROW(Raster<double>(kCols, kRows, broken), std::invalid_argument);
}

TEST(RasterTest, CellCentresAreOffsetByHalfACell) {
  const Raster<double> pasture = test_raster();
  const GeoTransform transform = pasture.transform();

  const Point2D first = pasture.cell_centre(0, 0);
  EXPECT_DOUBLE_EQ(first.easting, transform.origin_easting + 5.0);
  EXPECT_DOUBLE_EQ(first.northing, transform.origin_northing - 5.0);

  // Row index grows southwards: northing decreases.
  const Point2D lower = pasture.cell_centre(0, 1);
  EXPECT_DOUBLE_EQ(lower.northing, transform.origin_northing - 15.0);
}

TEST(RasterTest, CellLookupRoundTripsThroughCoordinates) {
  const Raster<double> pasture = test_raster();

  for (std::size_t row = 0; row < kRows; row += 7) {
    for (std::size_t col = 0; col < kCols; col += 11) {
      // Compared as optionals: a miss and a wrong cell are different failures,
      // and neither needs the value unwrapped to be reported.
      EXPECT_EQ(pasture.cell_at(pasture.cell_centre(col, row)),
                std::make_optional(CellIndex{col, row}));
    }
  }
}

TEST(RasterTest, CoordinatesOutsideTheExtentHaveNoCell) {
  const Raster<double> pasture = test_raster();
  const GeoTransform transform = pasture.transform();

  EXPECT_FALSE(pasture.cell_at({transform.origin_easting - 1.0, transform.origin_northing - 5.0})
                   .has_value());
  EXPECT_FALSE(pasture.cell_at({transform.origin_easting + 5.0, transform.origin_northing + 1.0})
                   .has_value());
  EXPECT_FALSE(
      pasture
          .cell_at({transform.origin_easting + (static_cast<double>(kCols) * kCellSize) + 1.0,
                    transform.origin_northing - 5.0})
          .has_value());
}

TEST(RasterTest, BoundsCoverEveryCellCentre) {
  const Raster<double> pasture = test_raster();
  const BoundingBox box = pasture.bounds();

  EXPECT_TRUE(box.contains(pasture.cell_centre(0, 0)));
  EXPECT_TRUE(box.contains(pasture.cell_centre(kCols - 1, kRows - 1)));
  EXPECT_DOUBLE_EQ(box.width(), static_cast<double>(kCols) * kCellSize);
  EXPECT_DOUBLE_EQ(box.height(), static_cast<double>(kRows) * kCellSize);
}

TEST(RasterTest, CheckedAccessRejectsCellsOutsideTheGrid) {
  Raster<double> pasture = test_raster();

  EXPECT_NO_THROW(pasture.at(kCols - 1, kRows - 1) = 1.0);
  EXPECT_THROW(static_cast<void>(pasture.at(kCols, 0)), std::out_of_range);
  EXPECT_THROW(static_cast<void>(pasture.at(0, kRows)), std::out_of_range);
  EXPECT_FALSE(pasture.contains(kCols, 0));
}

TEST(RasterTest, TraversalIsRowMajorAndCoversEveryCell) {
  Raster<double> pasture = test_raster();
  std::vector<std::size_t> visit_order;
  visit_order.reserve(pasture.size());

  pasture.for_each([&visit_order](std::size_t col, std::size_t row, double& value) {
    value = static_cast<double>((row * kCols) + col);
    visit_order.push_back((row * kCols) + col);
  });

  ASSERT_EQ(visit_order.size(), pasture.size());
  for (std::size_t i = 0; i < visit_order.size(); ++i) {
    EXPECT_EQ(visit_order[i], i);
  }
  EXPECT_DOUBLE_EQ(pasture(kCols - 1, kRows - 1), static_cast<double>(pasture.size() - 1));
}

TEST(RasterTest, FillOverwritesEveryCell) {
  Raster<double> pasture = test_raster(1.0);
  pasture.fill(2400.0);

  for (const double value : pasture.values()) {
    EXPECT_DOUBLE_EQ(value, 2400.0);
  }
}

}  // namespace
}  // namespace paddock::core
