// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <paddock/config/FarmConfig.hpp>
#include <paddock/core/Geometry.hpp>
#include <paddock/core/SyntheticTerrain.hpp>

#include "TomlSupport.hpp"

namespace paddock::config {

namespace {

/// New Zealand's extent in NZTM2000, generously rounded outwards.
///
/// This is a sanity bound, not a boundary: it catches the coordinate mistakes
/// that actually happen - easting and northing transposed, a latitude/longitude
/// pair left unprojected, a sign dropped - rather than defining the country.
/// EPSG:2193 declares axis order (northing, easting), so a farm at
/// `centre_easting = 5815700` is a transposition, and saying so by name is more
/// use than a raster that silently comes back empty.
constexpr double kMinEasting = 1000000.0;
constexpr double kMaxEasting = 2600000.0;
constexpr double kMinNorthing = 4700000.0;
constexpr double kMaxNorthing = 6300000.0;

/// New Zealand spans roughly 34S to 47S; the bound is loosened to the tenth.
constexpr double kMinLatitude = -48.0;
constexpr double kMaxLatitude = -33.0;

BoundarySource parse_boundary_kind(const std::string& kind, const toml::table& table,
                                   const std::string& path) {
  if (kind == "synthetic") {
    return BoundarySource::Synthetic;
  }
  if (kind == "inline") {
    return BoundarySource::Inline;
  }
  if (kind == "geopackage") {
    return BoundarySource::GeoPackage;
  }
  detail::throw_in(
      table, path,
      "unknown boundary kind '" + kind + "'. Known kinds are: synthetic, inline, geopackage");
}

/// One paddock from a `[[paddock]]` entry.
core::Paddock read_paddock(const toml::table& entry, const std::string& path, std::size_t index) {
  detail::reject_unknown_keys(entry, {"name", "vertices"}, path, "[[paddock]]");

  core::Paddock paddock;
  paddock.name = detail::require_string(entry, "name", path);

  const toml::node* vertices_node = entry.get("vertices");
  if (vertices_node == nullptr) {
    detail::throw_in(entry, path, "paddock '" + paddock.name + "' has no 'vertices'");
  }
  const toml::array* vertices = vertices_node->as_array();
  if (vertices == nullptr) {
    detail::throw_at(*vertices_node, path,
                     "'vertices' must be an array of [easting, northing] pairs");
  }

  std::vector<core::Point2D> points;
  points.reserve(vertices->size());
  for (const toml::node& element : *vertices) {
    const toml::array* pair = element.as_array();
    if (pair == nullptr || pair->size() != 2) {
      detail::throw_at(element, path,
                       "each vertex must be a pair [easting, northing], NZTM2000 metres");
    }
    // Written easting first, which is the order this project reads coordinates
    // in throughout - not the (northing, easting) order EPSG:2193 declares.
    // The distinction is documented in docs/adr and enforced by the bounds
    // check below, which is where a transposed file is actually caught.
    const auto coordinate = [&](std::size_t at) {
      const toml::node& value = *pair->get(at);
      if (value.is_floating_point()) {
        return value.as_floating_point()->get();
      }
      if (value.is_integer()) {
        return static_cast<double>(value.as_integer()->get());
      }
      detail::throw_at(value, path, "a coordinate must be a number");
    };
    points.push_back(core::Point2D{coordinate(0), coordinate(1)});
  }

  paddock.boundary = core::Polygon(std::move(points));
  if (!paddock.boundary.is_valid()) {
    detail::throw_in(entry, path,
                     "paddock '" + paddock.name + "' (entry " + std::to_string(index + 1) +
                         ") is not a valid polygon: it needs at least three distinct vertices "
                         "forming a ring with a positive area");
  }
  return paddock;
}

FarmDefinition read(const toml::table& root, const std::string& path) {
  detail::reject_unknown_keys(root, {"farm", "location", "boundary", "paddock"}, path, "the file");

  const toml::table& farm = detail::require_table(root, "farm", path);
  detail::reject_unknown_keys(
      farm, {"name", "display_name", "description", "region", "effective_hectares"}, path,
      "[farm]");

  FarmDefinition definition;
  definition.name = detail::require_string(farm, "name", path);
  definition.display_name = detail::optional_string(farm, "display_name", definition.name);
  definition.description = detail::optional_string(farm, "description", "");
  definition.region = detail::require_string(farm, "region", path);
  if (detail::has(farm, "effective_hectares")) {
    definition.stated_effective_hectares = detail::require_double(farm, "effective_hectares", path);
  }

  const toml::table& location = detail::require_table(root, "location", path);
  detail::reject_unknown_keys(
      location,
      {"centre_easting", "centre_northing", "latitude_degrees", "location_verified", "source"},
      path, "[location]");

  definition.location.centre_easting = detail::require_double(location, "centre_easting", path);
  definition.location.centre_northing = detail::require_double(location, "centre_northing", path);
  definition.location.latitude_degrees = detail::require_double(location, "latitude_degrees", path);
  definition.location.location_verified =
      detail::optional_bool(location, "location_verified", false, path);
  definition.location.source = detail::optional_string(location, "source", "");

  // A farm claiming a surveyed location has to say where the survey came from.
  // Without this the flag is free to set and means nothing.
  if (definition.location.location_verified && definition.location.source.empty()) {
    detail::throw_in(location, path,
                     "'location_verified = true' requires a 'source' saying where the "
                     "coordinates came from");
  }

  const toml::table& boundary = detail::require_table(root, "boundary", path);
  const std::string kind = detail::require_string(boundary, "kind", path);
  definition.boundary_source = parse_boundary_kind(kind, boundary, path);

  switch (definition.boundary_source) {
    case BoundarySource::Synthetic: {
      detail::reject_unknown_keys(boundary,
                                  {"kind", "extent_width_m", "extent_height_m", "paddock_hectares",
                                   "area_hectares", "area_source"},
                                  path, "[boundary] with kind = \"synthetic\"");
      definition.extent_width_m = detail::require_double(boundary, "extent_width_m", path);
      definition.extent_height_m = detail::require_double(boundary, "extent_height_m", path);
      definition.synthetic_paddock_hectares =
          detail::optional_double(boundary, "paddock_hectares", 2.5, path);
      definition.area_hectares = detail::optional_double(boundary, "area_hectares", 0.0, path);
      definition.area_source = detail::optional_string(boundary, "area_source", "");
      break;
    }
    case BoundarySource::Inline: {
      detail::reject_unknown_keys(boundary, {"kind"}, path, "[boundary] with kind = \"inline\"");
      const toml::node* paddocks_node = root.get("paddock");
      const toml::array* paddocks = paddocks_node == nullptr ? nullptr : paddocks_node->as_array();
      if (paddocks == nullptr || paddocks->empty()) {
        detail::throw_in(boundary, path, "kind = \"inline\" needs at least one [[paddock]] entry");
      }
      std::size_t index = 0;
      for (const toml::node& element : *paddocks) {
        const toml::table* entry = element.as_table();
        if (entry == nullptr) {
          detail::throw_at(element, path, "each [[paddock]] must be a table");
        }
        definition.paddocks.push_back(read_paddock(*entry, path, index));
        ++index;
      }
      break;
    }
    case BoundarySource::GeoPackage: {
      detail::reject_unknown_keys(boundary, {"kind", "path", "layer", "sha256"}, path,
                                  "[boundary] with kind = \"geopackage\"");
      definition.boundary_path = detail::require_string(boundary, "path", path);
      definition.boundary_layer = detail::require_string(boundary, "layer", path);
      // The hash is required rather than optional: a GeoPackage reference
      // without one is a path into a gitignored directory whose contents
      // nobody can check, which is exactly the reproducibility hole the
      // scenario bundles close.
      definition.boundary_sha256 = detail::require_string(boundary, "sha256", path);
      break;
    }
  }

  if (definition.boundary_source != BoundarySource::Inline && detail::has(root, "paddock")) {
    detail::throw_in(boundary, path,
                     "[[paddock]] entries are only read when kind = \"inline\"; this farm "
                     "declares kind = \"" +
                         kind + "\", so they would be silently ignored");
  }

  detail::require_valid(definition.validation_error(), farm, path);
  return definition;
}

}  // namespace

std::string to_string(BoundarySource source) {
  switch (source) {
    case BoundarySource::Synthetic:
      return "synthetic";
    case BoundarySource::Inline:
      return "inline";
    case BoundarySource::GeoPackage:
      return "geopackage";
  }
  return "unknown";
}

std::string FarmDefinition::validation_error() const {
  if (name.empty()) {
    return "a farm needs a 'name'";
  }
  if (region.empty()) {
    return "farm '" + name + "' needs a 'region'";
  }

  if (location.centre_easting < kMinEasting || location.centre_easting > kMaxEasting) {
    return "farm '" + name + "' has a centre_easting of " +
           std::to_string(location.centre_easting) +
           " m, outside New Zealand in NZTM2000. Coordinates are written easting first here; "
           "EPSG:2193 declares (northing, easting), so a transposed pair looks like this";
  }
  if (location.centre_northing < kMinNorthing || location.centre_northing > kMaxNorthing) {
    return "farm '" + name + "' has a centre_northing of " +
           std::to_string(location.centre_northing) + " m, outside New Zealand in NZTM2000";
  }
  if (location.latitude_degrees < kMinLatitude || location.latitude_degrees > kMaxLatitude) {
    return "farm '" + name + "' has a latitude of " + std::to_string(location.latitude_degrees) +
           " degrees, outside New Zealand. Southern latitudes are negative";
  }

  switch (boundary_source) {
    case BoundarySource::Synthetic:
      if (extent_width_m <= 0.0 || extent_height_m <= 0.0) {
        return "farm '" + name + "' needs a positive extent to generate paddocks over";
      }
      if (synthetic_paddock_hectares <= 0.0) {
        return "farm '" + name + "' needs a positive 'paddock_hectares'";
      }
      if (area_hectares > 0.0) {
        const double rectangle_hectares = extent_width_m * extent_height_m / 10000.0;
        // A tenth of a hectare. Tight enough that a wrong extent cannot hide
        // behind it, loose enough that a farm may state a round area and use
        // round metres for the rectangle.
        if (std::abs(rectangle_hectares - area_hectares) > 0.1) {
          return "farm '" + name + "' says it is " + std::to_string(area_hectares) +
                 " ha but its extent covers " + std::to_string(rectangle_hectares) +
                 " ha. The area is cited and the rectangle is invented, so it is the "
                 "rectangle that is wrong";
        }
      }
      if (!area_source.empty() && area_hectares <= 0.0) {
        return "farm '" + name + "' cites a source for its area without stating one";
      }
      break;
    case BoundarySource::Inline:
      if (paddocks.empty()) {
        return "farm '" + name + "' declares inline boundaries but lists no paddocks";
      }
      break;
    case BoundarySource::GeoPackage:
      if (boundary_path.empty() || boundary_layer.empty() || boundary_sha256.empty()) {
        return "farm '" + name + "' needs a 'path', a 'layer' and a 'sha256' for its GeoPackage";
      }
      break;
  }

  if (stated_effective_hectares.has_value() && stated_effective_hectares.value() <= 0.0) {
    return "farm '" + name + "' has a non-positive 'effective_hectares'";
  }
  return {};
}

std::vector<core::Paddock> FarmDefinition::make_paddocks() const {
  switch (boundary_source) {
    case BoundarySource::Inline:
      return paddocks;
    case BoundarySource::Synthetic: {
      core::BoundingBox area = core::BoundingBox::empty();
      // The centre is what the file records, so the extent is laid out around
      // it rather than from a corner: moving a farm should not also resize it.
      area.expand_to_include(core::Point2D{location.centre_easting - (extent_width_m / 2.0),
                                           location.centre_northing - (extent_height_m / 2.0)});
      area.expand_to_include(core::Point2D{location.centre_easting + (extent_width_m / 2.0),
                                           location.centre_northing + (extent_height_m / 2.0)});
      return core::SyntheticParcelSource(synthetic_paddock_hectares).fetch(area);
    }
    case BoundarySource::GeoPackage:
      break;
  }
  throw std::runtime_error(
      "farm '" + name +
      "' reads its boundaries from a GeoPackage, which needs GDAL: load it through gis/ rather "
      "than config/");
}

double FarmDefinition::boundary_hectares() const {
  double total = 0.0;
  for (const core::Paddock& paddock : make_paddocks()) {
    total += paddock.area_hectares();
  }
  return total;
}

FarmDefinition parse_farm(std::string_view text, const std::string& path) {
  return read(detail::parse_text(text, path), path);
}

FarmDefinition load_farm(const std::string& path) {
  return read(detail::parse_file(path), path);
}

std::vector<FarmDefinition> load_farms(const std::string& directory) {
  namespace fs = std::filesystem;

  std::error_code error;
  if (!fs::is_directory(directory, error)) {
    throw ConfigError(directory, 1, 1, "not a directory of farm descriptions");
  }

  // Collected and sorted before loading, because a directory iterator's order
  // is whatever the filesystem says. A farm list that changes order between
  // machines would make every report that prints it non-reproducible.
  std::vector<std::string> files;
  for (const fs::directory_entry& entry : fs::directory_iterator(directory)) {
    if (entry.is_regular_file() && entry.path().extension() == ".toml") {
      files.push_back(entry.path().string());
    }
  }
  std::sort(files.begin(), files.end());

  std::vector<FarmDefinition> farms;
  farms.reserve(files.size());
  std::map<std::string, std::string> seen;
  for (const std::string& file : files) {
    FarmDefinition farm = load_farm(file);
    const auto [existing, inserted] = seen.emplace(farm.name, file);
    if (!inserted) {
      throw ConfigError(file, 1, 1,
                        "farm name '" + farm.name + "' is already used by " + existing->second +
                            ". Names identify farms to scenarios, so they have to be unique");
    }
    farms.push_back(std::move(farm));
  }

  std::sort(farms.begin(), farms.end(), [](const FarmDefinition& lhs, const FarmDefinition& rhs) {
    return lhs.name < rhs.name;
  });
  return farms;
}

}  // namespace paddock::config
