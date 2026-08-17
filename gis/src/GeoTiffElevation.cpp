// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#include <algorithm>
#include <array>
#include <cmath>
#include <cpl_conv.h>
#include <cstddef>
#include <gdal_priv.h>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <paddock/gis/Environment.hpp>
#include <paddock/gis/GeoTiffElevation.hpp>

namespace paddock::gis {

namespace {

/// GDAL hands back a raw pointer that has to be closed with its own function,
/// which is exactly what unique_ptr with a custom deleter is for. Every early
/// return below is a leak without it.
struct DatasetCloser {
  void operator()(GDALDataset* dataset) const noexcept {
    if (dataset != nullptr) {
      GDALClose(dataset);
    }
  }
};

using DatasetHandle = std::unique_ptr<GDALDataset, DatasetCloser>;

DatasetHandle open_read_only(const std::string& path) {
  GDALAllRegister();
  return DatasetHandle{static_cast<GDALDataset*>(GDALOpen(path.c_str(), GA_ReadOnly))};
}

/// A GeoTIFF's geotransform, in the six numbers GDAL uses.
struct GeoTransformCoefficients {
  double origin_easting = 0.0;
  double pixel_width = 0.0;
  double row_rotation = 0.0;
  double origin_northing = 0.0;
  double column_rotation = 0.0;
  double pixel_height = 0.0;  ///< Negative for a north-up raster
};

bool read_transform(GDALDataset& dataset, GeoTransformCoefficients& out) {
  std::array<double, 6> coefficients{};
  if (dataset.GetGeoTransform(coefficients.data()) != CE_None) {
    return false;
  }
  out.origin_easting = coefficients[0];
  out.pixel_width = coefficients[1];
  out.row_rotation = coefficients[2];
  out.origin_northing = coefficients[3];
  out.column_rotation = coefficients[4];
  out.pixel_height = coefficients[5];
  return true;
}

/// Tolerance for "this point is inside the file", in metres. A request that
/// misses coverage by less than a millimetre is a rounding artefact of the
/// caller's arithmetic, not a request for ground the file does not have.
constexpr double kCoverageToleranceM = 1e-3;

}  // namespace

GeoTiffElevationSource::GeoTiffElevationSource(std::string path) : path_(std::move(path)) {}

core::SourceDescription GeoTiffElevationSource::describe() const {
  return core::SourceDescription{
      "LINZ elevation snapshot (GeoTIFF)",
      "Sourced from the LINZ Data Service and licensed for re-use under the Creative Commons "
      "Attribution 4.0 International licence.",
      "Whatever the file covers: " + path_,
      "Fixed. A snapshot does not change; re-fetch with scripts/linz-snapshot.py to update it."};
}

core::ConnectionStatus GeoTiffElevationSource::test_connection() const {
  if (!gdal_driver_available("GTiff")) {
    return core::ConnectionStatus::unavailable(
        "This GDAL was built without the GTiff driver, so no GeoTIFF can be read. Install a GDAL "
        "with GeoTIFF support.");
  }

  const DatasetHandle dataset = open_read_only(path_);
  if (!dataset) {
    return core::ConnectionStatus::unavailable(
        "Cannot open " + path_ +
        ". Snapshots are not committed - fetch one with scripts/linz-snapshot.py, or point the "
        "scenario at a file that exists.");
  }

  if (dataset->GetRasterCount() < 1) {
    return core::ConnectionStatus::unavailable(path_ +
                                               " has no raster bands to read elevation "
                                               "from.");
  }

  GeoTransformCoefficients transform;
  if (!read_transform(*dataset, transform)) {
    return core::ConnectionStatus::unavailable(
        path_ +
        " has no geotransform, so there is no way to know what ground it covers. A DEM "
        "without georeferencing cannot be used.");
  }

  const core::BoundingBox area = coverage();
  return core::ConnectionStatus::available(
      "Readable, covering " + std::to_string(static_cast<long long>(area.width())) + " m by " +
      std::to_string(static_cast<long long>(area.height())) + " m.");
}

core::BoundingBox GeoTiffElevationSource::coverage() const {
  core::BoundingBox area = core::BoundingBox::empty();

  const DatasetHandle dataset = open_read_only(path_);
  if (!dataset) {
    return area;
  }
  GeoTransformCoefficients transform;
  if (!read_transform(*dataset, transform)) {
    return area;
  }

  const auto cols = static_cast<double>(dataset->GetRasterXSize());
  const auto rows = static_cast<double>(dataset->GetRasterYSize());

  // pixel_height is negative for a north-up raster, so this reaches the
  // southern edge by adding it rather than subtracting.
  area.expand_to_include(core::Point2D{transform.origin_easting, transform.origin_northing});
  area.expand_to_include(
      core::Point2D{transform.origin_easting + (cols * transform.pixel_width),
                    transform.origin_northing + (rows * transform.pixel_height)});
  return area;
}

core::Raster<double> GeoTiffElevationSource::fetch(const core::BoundingBox& area,
                                                   double cell_size_m) const {
  if (area.is_empty() || area.width() <= 0.0 || area.height() <= 0.0) {
    throw std::out_of_range("GeoTiffElevationSource::fetch was given an empty area");
  }
  if (!(cell_size_m > 0.0)) {
    throw std::out_of_range("GeoTiffElevationSource::fetch needs a positive cell size");
  }

  const DatasetHandle dataset = open_read_only(path_);
  if (!dataset) {
    throw std::runtime_error("Cannot open " + path_ + ": " + test_connection().message);
  }

  GeoTransformCoefficients transform;
  if (!read_transform(*dataset, transform)) {
    throw std::runtime_error(path_ + " has no geotransform");
  }
  if (transform.row_rotation != 0.0 || transform.column_rotation != 0.0) {
    throw std::runtime_error(
        path_ +
        " is a rotated raster. Every LINZ DEM is north-up; a rotated one would need "
        "resampling this reader does not do, and silently ignoring the rotation would "
        "move the farm.");
  }

  const core::BoundingBox available = coverage();
  if (area.min_easting < available.min_easting - kCoverageToleranceM ||
      area.min_northing < available.min_northing - kCoverageToleranceM ||
      area.max_easting > available.max_easting + kCoverageToleranceM ||
      area.max_northing > available.max_northing + kCoverageToleranceM) {
    throw std::out_of_range(
        "Requested area is not fully inside " + path_ +
        ". A partly covered DEM would read as flat ground outside the file, which is a slope of "
        "zero and no error at all.");
  }

  const auto cols = static_cast<std::size_t>(std::ceil((area.width() / cell_size_m) - 1e-9));
  const auto rows = static_cast<std::size_t>(std::ceil((area.height() / cell_size_m) - 1e-9));

  core::GeoTransform out_transform;
  out_transform.origin_easting = area.min_easting;
  out_transform.origin_northing = area.min_northing + (static_cast<double>(rows) * cell_size_m);
  out_transform.cell_size = cell_size_m;

  core::Raster<double> elevation(cols, rows, out_transform);

  GDALRasterBand* band = dataset->GetRasterBand(1);
  if (band == nullptr) {
    throw std::runtime_error(path_ + " has no first raster band");
  }

  // Read the window covering the request once, then sample inside it.
  //
  // The obvious loop calls RasterIO per output cell, which is one GDAL call and
  // one block lookup each: twenty thousand of them for a 200 ha farm at 10 m.
  // One windowed read is a single pass over the part of the file that is
  // actually needed, and never reads the rest of a large DEM tile.
  const auto first_col = static_cast<int>(
      std::floor((area.min_easting - transform.origin_easting) / transform.pixel_width));
  const auto last_col = static_cast<int>(
      std::ceil((area.max_easting - transform.origin_easting) / transform.pixel_width));
  const auto first_row = static_cast<int>(
      std::floor((area.max_northing - transform.origin_northing) / transform.pixel_height));
  const auto last_row = static_cast<int>(
      std::ceil((area.min_northing - transform.origin_northing) / transform.pixel_height));

  const int window_col = std::clamp(first_col, 0, dataset->GetRasterXSize() - 1);
  const int window_row = std::clamp(first_row, 0, dataset->GetRasterYSize() - 1);
  const int window_cols =
      std::clamp(last_col - window_col + 1, 1, dataset->GetRasterXSize() - window_col);
  const int window_rows =
      std::clamp(last_row - window_row + 1, 1, dataset->GetRasterYSize() - window_row);

  std::vector<double> window(static_cast<std::size_t>(window_cols) *
                             static_cast<std::size_t>(window_rows));
  if (band->RasterIO(GF_Read, window_col, window_row, window_cols, window_rows, window.data(),
                     window_cols, window_rows, GDT_Float64, 0, 0) != CE_None) {
    throw std::runtime_error("Failed reading " + path_ + ": " + std::string(CPLGetLastErrorMsg()));
  }

  // Nearest neighbour at cell centres. Bilinear would be smoother and is what a
  // resampling step should eventually do, but it invents values between measured
  // ones, and this reader's job is to report what the DEM says. Recorded in
  // docs/verify.md rather than left as a silent choice.
  for (std::size_t row = 0; row < rows; ++row) {
    const double north =
        out_transform.origin_northing - ((static_cast<double>(row) + 0.5) * cell_size_m);
    const auto source_row =
        static_cast<int>(std::floor((north - transform.origin_northing) / transform.pixel_height));
    const std::size_t window_y =
        static_cast<std::size_t>(std::clamp(source_row - window_row, 0, window_rows - 1));

    for (std::size_t col = 0; col < cols; ++col) {
      const double east = area.min_easting + ((static_cast<double>(col) + 0.5) * cell_size_m);
      const auto source_col =
          static_cast<int>(std::floor((east - transform.origin_easting) / transform.pixel_width));
      const std::size_t window_x =
          static_cast<std::size_t>(std::clamp(source_col - window_col, 0, window_cols - 1));

      elevation(col, row) = window[(window_y * static_cast<std::size_t>(window_cols)) + window_x];
    }
  }

  return elevation;
}

}  // namespace paddock::gis
