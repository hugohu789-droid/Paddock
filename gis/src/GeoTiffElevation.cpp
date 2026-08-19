// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#include <algorithm>
#include <array>
#include <cmath>
#include <cpl_conv.h>
#include <cstddef>
#include <filesystem>
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
      // Not the Data Service: LINZ publishes elevation as open data, and this
      // is where these files come from. Each capture also names its own
      // licensor - Environment Canterbury for the Canterbury LiDAR - so the
      // attribution a report actually owes is the one written beside the file,
      // and this line says where to find it rather than guessing at it.
      "Creative Commons Attribution 4.0 International. LINZ elevation is open data; the licensor "
      "and the survey dates differ by capture and are recorded in the .provenance.json written "
      "beside the snapshot.",
      "Whatever the file covers: " + path_,
      "Fixed. A snapshot does not change; re-fetch with scripts/nz-elevation-snapshot.py to "
      "update it."};
}

core::ConnectionStatus GeoTiffElevationSource::test_connection() const {
  if (!gdal_driver_available("GTiff")) {
    return core::ConnectionStatus::unavailable(
        "This GDAL was built without the GTiff driver, so no GeoTIFF can be read. Install a GDAL "
        "with GeoTIFF support.");
  }

  const DatasetHandle dataset = open_read_only(path_);
  if (!dataset) {
    // A file that is missing and a file that cannot be decoded both fail to
    // open, and telling a person to fetch a snapshot they already have sends
    // them the wrong way entirely. It happened: LINZ compresses its 1 m
    // elevation with LERC, a libtiff without that codec refuses the file, and
    // the only report was "fetch one".
    if (!std::filesystem::exists(path_)) {
      return core::ConnectionStatus::unavailable(
          path_ +
          " is not there. Snapshots are not committed - fetch one with "
          "scripts/nz-elevation-snapshot.py, or point the scenario at a file that exists.");
    }
    return core::ConnectionStatus::unavailable(
        path_ +
        " is there and GDAL will not open it. The usual cause is a compression this build cannot "
        "decode: LINZ elevation is LERC compressed, which needs libtiff built with that codec - "
        "see the gis feature in vcpkg.json. GDAL states the reason it refused on stderr.");
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

  // The mean of the source pixels inside each cell, not the one pixel under its
  // centre.
  //
  // **Why this changed.** It used to take the single pixel nearest the cell
  // centre, on the reasoning that averaging is a kind of invention and this
  // reader's job is to report what the DEM says. That reasoning was wrong about
  // which quantity is being asked for. A 25 m cell over a 1 m DEM covers 625
  // measured pixels, and the centre one is 0.16% of the ground the cell stands
  // for: a molehill, a wheel rut or a single noisy return became the height of
  // a quarter-hectare. What came out was visibly rough on a farm that is not,
  // and the roughness fed the slope, which feeds the energy cost of walking and
  // the radiation a face receives.
  //
  // A mean over the cell is not interpolation. Bilinear would invent values
  // BETWEEN measured points and is still not done here; every number in this
  // average is a measured pixel inside the cell it is reported for. It is the
  // ordinary meaning of "the height of this cell".
  //
  // Where a cell is smaller than a source pixel there is nothing to average, so
  // it falls back to the pixel the cell centre lands in - the old behaviour, in
  // the one case where it was the only thing available.
  for (std::size_t row = 0; row < rows; ++row) {
    const double north_edge =
        out_transform.origin_northing - (static_cast<double>(row) * cell_size_m);
    const double south_edge = north_edge - cell_size_m;

    for (std::size_t col = 0; col < cols; ++col) {
      const double west_edge = area.min_easting + (static_cast<double>(col) * cell_size_m);
      const double east_edge = west_edge + cell_size_m;

      // Source pixels whose centres fall inside the cell. Centres rather than
      // any overlap, so that every source pixel is counted by exactly one cell
      // and no pixel is weighed twice on a shared edge.
      const auto first_source_col = static_cast<int>(
          std::ceil(((west_edge - transform.origin_easting) / transform.pixel_width) - 0.5));
      const auto last_source_col = static_cast<int>(
          std::floor(((east_edge - transform.origin_easting) / transform.pixel_width) - 0.5));
      const auto first_source_row = static_cast<int>(
          std::ceil(((north_edge - transform.origin_northing) / transform.pixel_height) - 0.5));
      const auto last_source_row = static_cast<int>(
          std::floor(((south_edge - transform.origin_northing) / transform.pixel_height) - 0.5));

      double total = 0.0;
      std::size_t counted = 0;
      for (int source_row = first_source_row; source_row <= last_source_row; ++source_row) {
        const int y = source_row - window_row;
        if (y < 0 || y >= window_rows) {
          continue;
        }
        for (int source_col = first_source_col; source_col <= last_source_col; ++source_col) {
          const int x = source_col - window_col;
          if (x < 0 || x >= window_cols) {
            continue;
          }
          total += window[(static_cast<std::size_t>(y) * static_cast<std::size_t>(window_cols)) +
                          static_cast<std::size_t>(x)];
          ++counted;
        }
      }

      if (counted > 0) {
        elevation(col, row) = total / static_cast<double>(counted);
        continue;
      }

      // Nothing inside the cell: it is finer than the DEM. Take the pixel the
      // centre lands in.
      const double north = north_edge - (cell_size_m / 2.0);
      const double east = west_edge + (cell_size_m / 2.0);
      const auto centre_row = static_cast<int>(
          std::floor((north - transform.origin_northing) / transform.pixel_height));
      const auto centre_col =
          static_cast<int>(std::floor((east - transform.origin_easting) / transform.pixel_width));
      const std::size_t window_y =
          static_cast<std::size_t>(std::clamp(centre_row - window_row, 0, window_rows - 1));
      const std::size_t window_x =
          static_cast<std::size_t>(std::clamp(centre_col - window_col, 0, window_cols - 1));
      elevation(col, row) = window[(window_y * static_cast<std::size_t>(window_cols)) + window_x];
    }
  }

  return elevation;
}

}  // namespace paddock::gis
