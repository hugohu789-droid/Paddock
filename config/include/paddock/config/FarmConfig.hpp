// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <paddock/core/Terrain.hpp>

namespace paddock::config {

/// Where a farm's paddock boundaries come from.
///
/// The three kinds are the three ways this project can honestly know where a
/// fence is, and each one is a different claim about provenance:
///
/// - `Synthetic` invents them. Useful, and never mistaken for a survey.
/// - `Inline` reads them from the farm file itself. This is the form a boundary
///   editor writes, and the form a hand-checked farm takes.
/// - `GeoPackage` points at a LINZ-derived snapshot by path and hash. The
///   snapshot is gitignored (CLAUDE.md forbids committing bulk data); the hash
///   is what makes the reference reproducible without it.
///
/// Adding a fourth kind is adding an enumerator and a branch. Adding a *farm*
/// is adding a file, which is the distinction that matters: the set of farms is
/// open, and nothing in the code enumerates it.
enum class BoundarySource {
  Synthetic,
  Inline,
  GeoPackage,
};

[[nodiscard]] std::string to_string(BoundarySource source);

/// A farm's position, in the two coordinate systems the model needs it in.
///
/// NZTM2000 metres are what the simulation works in; latitude is what solar
/// geometry needs. The second is derivable from the first, but only by code
/// that can project, and `core` deliberately cannot (CLAUDE.md, principle 1) -
/// so it is declared here and cross-checked in the `gis` suite, which can.
struct FarmLocation {
  double centre_easting = 0.0;
  double centre_northing = 0.0;
  double latitude_degrees = 0.0;

  /// False when the coordinates are approximate rather than surveyed. A farm
  /// carrying an unverified location still loads and still runs; what it must
  /// not do is let the number pass as measured. Listed in docs/verify.md until
  /// it is true.
  bool location_verified = false;

  /// Where the coordinates came from, quoted in error messages and reports.
  std::string source;
};

/// A farm: who it is, where it is, and where its fences come from.
///
/// This is the unit the boundary editor of task #22 will read and write. The
/// editor does not exist yet; the format it edits does, which is the point.
/// Nothing downstream should ever learn a farm's identity from a hard-coded
/// name.
struct FarmDefinition {
  /// Stable identifier, snake_case, unique within a farm directory. This is
  /// what a scenario refers to; the display name may change without breaking
  /// anything.
  std::string name;
  std::string display_name;
  std::string description;
  std::string region;

  FarmLocation location;

  BoundarySource boundary_source = BoundarySource::Synthetic;

  /// Present when boundary_source is Inline. Already validated: closed rings,
  /// no self-intersection reported by Polygon::is_valid, positive area.
  std::vector<core::Paddock> paddocks;

  /// Present when boundary_source is GeoPackage: a path relative to the
  /// repository root - conventionally under the gitignored data/snapshots/ -
  /// with the layer to read and the hash the farm was described against.
  std::string boundary_path;
  std::string boundary_layer;
  std::string boundary_sha256;

  /// Present when boundary_source is Synthetic: the extent to generate over and
  /// the size of the paddocks to cut it into.
  double extent_width_m = 0.0;
  double extent_height_m = 0.0;
  double synthetic_paddock_hectares = 0.0;

  /// The farm's total area where it is known independently of the boundaries -
  /// a published effective hectares, say. Compared against the boundaries when
  /// both are available, which is a real check rather than a restatement.
  std::optional<double> stated_effective_hectares;

  /// Empty when the definition is self-consistent; otherwise what is wrong.
  [[nodiscard]] std::string validation_error() const;

  /// The paddocks this farm describes, generating them when the source is
  /// synthetic. Throws for a GeoPackage source: reading one needs GDAL, so it
  /// belongs to `gis`, and a config module that pretended otherwise would drag
  /// the dependency into every build.
  [[nodiscard]] std::vector<core::Paddock> make_paddocks() const;

  /// Total area of the paddocks, hectares.
  [[nodiscard]] double boundary_hectares() const;
};

/// Reads one farm description.
///
/// ```toml
/// [farm]
/// name = "ruakura_research_centre"
/// display_name = "Ruakura Research Centre"
/// description = "Flat Waikato dairy research land."
/// region = "Waikato"
///
/// [location]
/// centre_easting = 1804011.1268
/// centre_northing = 5815700.0870
/// latitude_degrees = -37.7833
/// location_verified = true
/// source = "LINZ Concord conversion; see tests/gis/ProjectionTest.cpp"
///
/// [boundary]
/// kind = "synthetic"          # or "inline", or "geopackage"
/// extent_width_m = 1600.0
/// extent_height_m = 1200.0
/// paddock_hectares = 2.5
///
/// # kind = "inline" instead takes a list of paddocks:
/// # [[paddock]]
/// # name = "North 1"
/// # vertices = [[1804000.0, 5815700.0], [1804200.0, 5815700.0],
/// #             [1804200.0, 5815575.0], [1804000.0, 5815575.0]]
///
/// # kind = "geopackage" instead points at a snapshot:
/// # path = "data/snapshots/ruakura-parcels.gpkg"
/// # layer = "nz_primary_parcels"
/// # sha256 = "..."
/// ```
[[nodiscard]] FarmDefinition load_farm(const std::string& path);

[[nodiscard]] FarmDefinition parse_farm(std::string_view text, const std::string& path);

/// Every farm description in a directory, sorted by name.
///
/// The farm set is discovered, not enumerated: dropping a new `.toml` into
/// `data/farms/` adds a farm, and no code changes. Throws ConfigError naming
/// the offending file if any of them fails to load, and if two files declare
/// the same `name`.
[[nodiscard]] std::vector<FarmDefinition> load_farms(const std::string& directory);

}  // namespace paddock::config
