#pragma once

#include <string_view>

/// Geospatial input and output: LINZ DEM and cadastre, Topo50 waterways,
/// Manaaki Whenua soils, NZTM2000 transforms. GDAL, PROJ and GEOS types stop at
/// this boundary — core sees only Raster<T> and Polygon.
namespace paddock::gis {

[[nodiscard]] std::string_view module_name() noexcept;

}  // namespace paddock::gis
