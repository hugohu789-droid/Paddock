// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#pragma once

#include <memory>

/// NZGD2000 geographic coordinates to and from NZTM2000 projected metres.
///
/// This is the boundary CLAUDE.md describes: everything inside the simulator
/// computes in NZTM2000 metres, and geographic coordinates are converted here
/// and nowhere else. No PROJ type appears in this header - the transform lives
/// behind a pointer to an incomplete type - so gis/ can keep linking PROJ
/// PRIVATE and nothing downstream needs a PROJ include path.
namespace paddock::gis {

/// A position on NZGD2000 (EPSG:4167), in degrees.
///
/// NZGD2000, deliberately, not WGS84. They are close enough to be confused and
/// far enough apart to matter: NZGD2000 is fixed to the Australian plate at
/// epoch 2000.0, while WGS84 tracks the ITRF, and New Zealand moves north-east
/// at roughly 5 cm a year. Two decades of that is metres - larger than a
/// paddock boundary is worth. LINZ publishes in NZGD2000, so that is what this
/// converts, and a WGS84 source (a phone's GPS, a web map click) needs its own
/// transform rather than being passed off as this one.
struct Geographic {
  double longitude_degrees = 0.0;
  double latitude_degrees = 0.0;
};

/// A position on NZTM2000 (EPSG:2193), in metres.
///
/// Easting first in this struct, because that is how New Zealand data is
/// written and read. Note that EPSG:2193 itself declares the opposite order -
/// (northing, easting) - which is what LINZ's own conversion API returns and
/// what GDAL 3 honours by default. Projection::to_nztm applies PROJ's
/// visualisation normalisation so this struct's fields always mean what their
/// names say; getting that wrong puts a farm in the wrong place without
/// producing an error. See docs/adr/0011-gdal-and-proj.md.
struct Nztm {
  double easting = 0.0;
  double northing = 0.0;
};

class Projection {
 public:
  /// Builds the NZGD2000 <-> NZTM2000 transform.
  ///
  /// Throws std::runtime_error if PROJ cannot build it, which in practice means
  /// proj.db is missing or unreadable. Construction is expensive - PROJ looks
  /// up and plans a coordinate operation - so build one and keep it rather than
  /// making one per point.
  Projection();
  ~Projection();

  Projection(const Projection&) = delete;
  Projection& operator=(const Projection&) = delete;
  Projection(Projection&&) noexcept;
  Projection& operator=(Projection&&) noexcept;

  [[nodiscard]] Nztm to_nztm(const Geographic& point) const;
  [[nodiscard]] Geographic to_geographic(const Nztm& point) const;

 private:
  struct Transform;
  std::unique_ptr<Transform> transform_;
};

}  // namespace paddock::gis
