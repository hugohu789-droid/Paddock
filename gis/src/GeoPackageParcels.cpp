// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#include <cpl_conv.h>
#include <cstddef>
#include <gdal_priv.h>
#include <memory>
#include <ogrsf_frmts.h>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <paddock/gis/Environment.hpp>
#include <paddock/gis/GeoPackageParcels.hpp>

namespace paddock::gis {

namespace {

struct DatasetCloser {
  void operator()(GDALDataset* dataset) const noexcept {
    if (dataset != nullptr) {
      GDALClose(dataset);
    }
  }
};

using DatasetHandle = std::unique_ptr<GDALDataset, DatasetCloser>;

DatasetHandle open_vector(const std::string& path) {
  GDALAllRegister();
  return DatasetHandle{static_cast<GDALDataset*>(
      GDALOpenEx(path.c_str(), GDAL_OF_VECTOR | GDAL_OF_READONLY, nullptr, nullptr, nullptr))};
}

OGRLayer* select_layer(GDALDataset& dataset, const std::string& layer) {
  if (!layer.empty()) {
    return dataset.GetLayerByName(layer.c_str());
  }
  return dataset.GetLayerCount() > 0 ? dataset.GetLayer(0) : nullptr;
}

/// Turns one OGR ring into a core Polygon.
///
/// The closing vertex is dropped: OGR repeats the first point at the end of a
/// ring, and core's Polygon closes implicitly. Leaving it in would add a
/// zero-length edge, which is harmless for area and a nuisance for everything
/// that iterates vertices.
core::Polygon ring_to_polygon(const OGRLinearRing& ring) {
  const int points = ring.getNumPoints();
  std::vector<core::Point2D> vertices;
  vertices.reserve(static_cast<std::size_t>(points));

  for (int i = 0; i < points; ++i) {
    vertices.push_back(core::Point2D{ring.getX(i), ring.getY(i)});
  }
  if (vertices.size() > 1 && vertices.front() == vertices.back()) {
    vertices.pop_back();
  }
  return core::Polygon{std::move(vertices)};
}

/// The outer ring of whatever geometry a feature carries.
///
/// A cadastral parcel is a polygon, and occasionally a multipolygon - a paddock
/// split by a road reserve, say. Only the largest part is taken, and the choice
/// is deliberate rather than incidental: core's Polygon is a single ring, and
/// silently keeping the first part of a multipolygon would sometimes keep a
/// sliver and drop the paddock.
bool largest_outer_ring(const OGRGeometry* geometry, core::Polygon& out) {
  if (geometry == nullptr) {
    return false;
  }

  switch (wkbFlatten(geometry->getGeometryType())) {
    case wkbPolygon: {
      const auto* polygon = geometry->toPolygon();
      const OGRLinearRing* ring = polygon->getExteriorRing();
      if (ring == nullptr) {
        return false;
      }
      out = ring_to_polygon(*ring);
      return true;
    }
    case wkbMultiPolygon: {
      const auto* multi = geometry->toMultiPolygon();
      double largest = 0.0;
      bool found = false;
      for (const OGRPolygon* part : multi) {
        const OGRLinearRing* ring = part->getExteriorRing();
        if (ring == nullptr) {
          continue;
        }
        core::Polygon candidate = ring_to_polygon(*ring);
        if (candidate.area() > largest) {
          largest = candidate.area();
          out = std::move(candidate);
          found = true;
        }
      }
      return found;
    }
    default:
      return false;
  }
}

}  // namespace

GeoPackageParcelSource::GeoPackageParcelSource(std::string path, std::string layer,
                                               std::string name_field)
    : path_(std::move(path)), layer_(std::move(layer)), name_field_(std::move(name_field)) {}

core::SourceDescription GeoPackageParcelSource::describe() const {
  return core::SourceDescription{
      "LINZ parcel snapshot (GeoPackage)",
      "Sourced from the LINZ Data Service and licensed for re-use under the Creative Commons "
      "Attribution 4.0 International licence.",
      "Whatever the file covers: " + path_,
      "Fixed. A snapshot does not change; re-fetch with scripts/linz-snapshot.py to update it."};
}

core::ConnectionStatus GeoPackageParcelSource::test_connection() const {
  if (!gdal_driver_available("GPKG")) {
    return core::ConnectionStatus::unavailable(
        "This GDAL was built without the GPKG driver, so no GeoPackage can be read. Install a GDAL "
        "with GeoPackage support.");
  }

  const DatasetHandle dataset = open_vector(path_);
  if (!dataset) {
    return core::ConnectionStatus::unavailable(
        "Cannot open " + path_ +
        ". Snapshots are not committed - fetch one with scripts/linz-snapshot.py, or point the "
        "scenario at a file that exists.");
  }

  OGRLayer* layer = select_layer(*dataset, layer_);
  if (layer == nullptr) {
    return core::ConnectionStatus::unavailable(
        layer_.empty() ? path_ + " has no layers to read parcels from."
                       : "No layer called \"" + layer_ + "\" in " + path_ + ".");
  }

  const GIntBig features = layer->GetFeatureCount(1);
  return core::ConnectionStatus::available("Readable, " + std::to_string(features) +
                                           " features in layer \"" + std::string(layer->GetName()) +
                                           "\".");
}

std::vector<core::Paddock> GeoPackageParcelSource::fetch(const core::BoundingBox& area) const {
  const DatasetHandle dataset = open_vector(path_);
  if (!dataset) {
    throw std::runtime_error("Cannot open " + path_ + ": " + test_connection().message);
  }

  OGRLayer* layer = select_layer(*dataset, layer_);
  if (layer == nullptr) {
    throw std::runtime_error("No usable layer in " + path_ + ": " + test_connection().message);
  }

  // Ask OGR to filter, rather than reading everything and discarding. On a
  // GeoPackage this uses the spatial index, so a farm-sized window out of a
  // regional parcel file does not walk every feature in the country.
  if (!area.is_empty()) {
    layer->SetSpatialFilterRect(area.min_easting, area.min_northing, area.max_easting,
                                area.max_northing);
  }
  layer->ResetReading();

  const int name_index =
      name_field_.empty() ? -1 : layer->GetLayerDefn()->GetFieldIndex(name_field_.c_str());

  std::vector<core::Paddock> paddocks;
  std::size_t ordinal = 0;
  while (OGRFeature* raw = layer->GetNextFeature()) {
    const std::unique_ptr<OGRFeature, decltype(&OGRFeature::DestroyFeature)> feature{
        raw, &OGRFeature::DestroyFeature};
    ++ordinal;

    core::Polygon boundary;
    if (!largest_outer_ring(feature->GetGeometryRef(), boundary)) {
      continue;
    }
    // A ring with fewer than three distinct vertices, or zero area, is not a
    // paddock. Cadastral data contains both; carrying them through would give
    // task #21 a mask with no cells and a division by zero downstream.
    if (!boundary.is_valid()) {
      continue;
    }

    std::string name;
    if (name_index >= 0 && feature->IsFieldSetAndNotNull(name_index)) {
      name = feature->GetFieldAsString(name_index);
    }
    if (name.empty()) {
      name = "Paddock " + std::to_string(ordinal);
    }

    paddocks.push_back(core::Paddock{std::move(name), std::move(boundary)});
  }

  return paddocks;
}

}  // namespace paddock::gis
