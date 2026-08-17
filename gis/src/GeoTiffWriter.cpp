// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#include <array>
#include <cpl_conv.h>
#include <cstddef>
#include <gdal_priv.h>
#include <ogr_spatialref.h>
#include <stdexcept>
#include <string>
#include <vector>

#include <paddock/gis/GeoTiffWriter.hpp>

namespace paddock::gis {

namespace {

/// EPSG code for New Zealand Transverse Mercator 2000. Everything inside the
/// model is in these metres, so everything written out is too.
constexpr int kNztm2000 = 2193;

}  // namespace

void write_geotiff(const core::Raster<double>& raster, const std::string& path) {
  if (raster.empty()) {
    throw std::runtime_error("Refusing to write an empty raster to " + path);
  }

  GDALAllRegister();
  GDALDriver* driver = GetGDALDriverManager()->GetDriverByName("GTiff");
  if (driver == nullptr) {
    throw std::runtime_error("This GDAL has no GTiff driver, so " + path + " cannot be written");
  }

  const auto cols = static_cast<int>(raster.cols());
  const auto rows = static_cast<int>(raster.rows());

  GDALDataset* dataset = driver->Create(path.c_str(), cols, rows, 1, GDT_Float64, nullptr);
  if (dataset == nullptr) {
    throw std::runtime_error("Cannot create " + path + ": " + std::string(CPLGetLastErrorMsg()));
  }

  // GDAL's six coefficients: origin easting, pixel width, row rotation, origin
  // northing, column rotation, pixel height. The height is negative because
  // row 0 is the northernmost - in a Paddock raster and in a north-up GeoTIFF
  // alike - and a positive one here would write the farm upside down while
  // looking perfectly well formed.
  const double cell = raster.transform().cell_size;
  std::array<double, 6> coefficients{raster.transform().origin_easting,  cell, 0.0,
                                     raster.transform().origin_northing, 0.0,  -cell};
  dataset->SetGeoTransform(coefficients.data());

  // A GeoTIFF without a coordinate reference system is not an export, whatever
  // else is right about it: QGIS opens it as an unplaced grid, and a reader has
  // no way to know the numbers are NZTM metres. So this is an error rather than
  // something to skip quietly - and the way it fails in practice is PROJ being
  // unable to read proj.db, which the message says.
  OGRSpatialReference nztm;
  char* wkt = nullptr;
  const bool projected = nztm.importFromEPSG(kNztm2000) == OGRERR_NONE &&
                         nztm.exportToWkt(&wkt) == OGRERR_NONE && wkt != nullptr;
  if (projected) {
    dataset->SetProjection(wkt);
  }
  CPLFree(wkt);
  if (!projected) {
    GDALClose(dataset);
    throw std::runtime_error(
        "Cannot describe EPSG:2193 while writing " + path +
        ", so the file would carry no coordinate reference system and open in QGIS as an unplaced "
        "grid. PROJ could not read proj.db; set PROJ_DATA to the directory holding it.");
  }

  std::vector<double> values(raster.size());
  for (std::size_t row = 0; row < raster.rows(); ++row) {
    for (std::size_t col = 0; col < raster.cols(); ++col) {
      values[(row * raster.cols()) + col] = raster(col, row);
    }
  }

  const CPLErr written = dataset->GetRasterBand(1)->RasterIO(
      GF_Write, 0, 0, cols, rows, values.data(), cols, rows, GDT_Float64, 0, 0);
  const std::string reason = written == CE_None ? std::string() : std::string(CPLGetLastErrorMsg());
  GDALClose(dataset);

  if (written != CE_None) {
    throw std::runtime_error("Failed writing pixels to " + path + ": " + reason);
  }
}

}  // namespace paddock::gis
