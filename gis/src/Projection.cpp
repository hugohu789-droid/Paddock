// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#include <proj.h>
#include <stdexcept>
#include <string>
#include <utility>

#include <paddock/gis/Projection.hpp>

namespace paddock::gis {

namespace {

/// NZGD2000 geographic 2D. Not EPSG:4326 - see the note on Geographic.
constexpr const char* kNzgd2000 = "EPSG:4167";

/// New Zealand Transverse Mercator 2000.
constexpr const char* kNztm2000 = "EPSG:2193";

}  // namespace

struct Projection::Transform {
  PJ* pj = nullptr;

  ~Transform() {
    if (pj != nullptr) {
      proj_destroy(pj);
    }
  }
};

Projection::Projection() : transform_(std::make_unique<Transform>()) {
  PJ* planned = proj_create_crs_to_crs(PJ_DEFAULT_CTX, kNzgd2000, kNztm2000, nullptr);
  if (planned == nullptr) {
    const int code = proj_context_errno(PJ_DEFAULT_CTX);
    throw std::runtime_error(std::string("PROJ could not build the NZGD2000 to NZTM2000 "
                                         "transform: ") +
                             proj_errno_string(code) +
                             ". This usually means proj.db is missing from PROJ's search path.");
  }

  // Both EPSG:4167 and EPSG:2193 declare latitude/northing first. Without this
  // call every coordinate would have to be swapped by hand at each call site,
  // which is precisely the kind of thing that is got right in four places and
  // wrong in the fifth. Normalising once here means Geographic and Nztm can
  // name their fields honestly.
  PJ* normalised = proj_normalize_for_visualization(PJ_DEFAULT_CTX, planned);
  proj_destroy(planned);
  if (normalised == nullptr) {
    const int code = proj_context_errno(PJ_DEFAULT_CTX);
    throw std::runtime_error(std::string("PROJ could not normalise the axis order of the "
                                         "NZGD2000 to NZTM2000 transform: ") +
                             proj_errno_string(code));
  }

  transform_->pj = normalised;
}

Projection::~Projection() = default;
Projection::Projection(Projection&&) noexcept = default;
Projection& Projection::operator=(Projection&&) noexcept = default;

Nztm Projection::to_nztm(const Geographic& point) const {
  // After normalisation the forward direction takes (longitude, latitude) and
  // returns (easting, northing).
  PJ_COORD in = proj_coord(point.longitude_degrees, point.latitude_degrees, 0.0, 0.0);
  const PJ_COORD out = proj_trans(transform_->pj, PJ_FWD, in);
  return Nztm{out.xy.x, out.xy.y};
}

Geographic Projection::to_geographic(const Nztm& point) const {
  PJ_COORD in = proj_coord(point.easting, point.northing, 0.0, 0.0);
  const PJ_COORD out = proj_trans(transform_->pj, PJ_INV, in);
  return Geographic{out.xy.x, out.xy.y};
}

}  // namespace paddock::gis
