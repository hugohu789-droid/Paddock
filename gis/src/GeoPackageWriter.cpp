// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#include <cpl_conv.h>
#include <cstddef>
#include <cstdio>
#include <gdal_priv.h>
#include <memory>
#include <ogr_spatialref.h>
#include <ogrsf_frmts.h>
#include <stdexcept>
#include <string>
#include <vector>

#include <paddock/gis/GeoPackageWriter.hpp>

namespace paddock::gis {

namespace {

constexpr int kNztm2000 = 2193;

struct DatasetCloser {
  void operator()(GDALDataset* dataset) const noexcept {
    if (dataset != nullptr) {
      GDALClose(dataset);
    }
  }
};

using DatasetHandle = std::unique_ptr<GDALDataset, DatasetCloser>;

}  // namespace

void write_geopackage(const std::vector<core::Paddock>& paddocks, const std::string& path,
                      const std::string& layer_name) {
  GDALAllRegister();
  GDALDriver* driver = GetGDALDriverManager()->GetDriverByName("GPKG");
  if (driver == nullptr) {
    throw std::runtime_error("This GDAL has no GPKG driver, so " + path + " cannot be written");
  }

  // GDAL will not overwrite a GeoPackage in place, and a stale layer left
  // beside a new one is worse than a clear failure.
  std::remove(path.c_str());

  DatasetHandle dataset{driver->Create(path.c_str(), 0, 0, 0, GDT_Unknown, nullptr)};
  if (!dataset) {
    throw std::runtime_error("Cannot create " + path + ": " + std::string(CPLGetLastErrorMsg()));
  }

  OGRSpatialReference nztm;
  if (nztm.importFromEPSG(kNztm2000) != OGRERR_NONE) {
    throw std::runtime_error(
        "Cannot describe EPSG:2193 while writing " + path +
        ", so the file would carry no coordinate reference system and open as an unplaced shape. "
        "PROJ could not read proj.db; set PROJ_DATA to the directory holding it.");
  }
  // GeoPackage stores axis order as the authority declares it unless told
  // otherwise, and EPSG:2193 declares (northing, easting). Everything in this
  // project is written easting first, so say so rather than let the file
  // disagree with its own coordinates.
  nztm.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);

  OGRLayer* layer = dataset->CreateLayer(layer_name.c_str(), &nztm, wkbPolygon, nullptr);
  if (layer == nullptr) {
    throw std::runtime_error("Cannot create layer \"" + layer_name + "\" in " + path + ": " +
                             std::string(CPLGetLastErrorMsg()));
  }

  OGRFieldDefn name_field("name", OFTString);
  name_field.SetWidth(120);
  if (layer->CreateField(&name_field) != OGRERR_NONE) {
    throw std::runtime_error("Cannot create the name field in " + path);
  }

  for (const core::Paddock& paddock : paddocks) {
    OGRLinearRing ring;
    for (const core::Point2D& vertex : paddock.boundary.vertices()) {
      ring.addPoint(vertex.easting, vertex.northing);
    }
    // OGR expects an explicitly closed ring; core's Polygon closes implicitly.
    ring.closeRings();

    OGRPolygon polygon;
    polygon.addRing(&ring);

    const std::unique_ptr<OGRFeature, decltype(&OGRFeature::DestroyFeature)> feature{
        OGRFeature::CreateFeature(layer->GetLayerDefn()), &OGRFeature::DestroyFeature};
    feature->SetField("name", paddock.name.c_str());
    feature->SetGeometry(&polygon);

    if (layer->CreateFeature(feature.get()) != OGRERR_NONE) {
      throw std::runtime_error("Cannot write paddock \"" + paddock.name + "\" to " + path + ": " +
                               std::string(CPLGetLastErrorMsg()));
    }
  }
}

}  // namespace paddock::gis
