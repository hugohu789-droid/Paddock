// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#pragma once

#include <cstdint>
#include <string>

#include <paddock/core/Geometry.hpp>

namespace paddock::core {

/// Components are plain data. They carry no behaviour and no defaults worth
/// arguing about: every number that describes a species, pasture, disease or
/// pest comes from a TOML definition under data/, cited to a published source.
/// Adding a species means adding a data file, never a class.

/// Where the entity is, in NZTM2000 metres.
struct Position {
  Point2D location;
};

/// Links an entity to its definition file, e.g. "sheep_romney" in
/// data/species/. Resolved at load time; the core never parses TOML itself.
struct SpeciesRef {
  std::string species_id;
};

/// Marks an entity that removes pasture dry matter by grazing.
struct Grazer {
  double intake_capacity_kg_dm_per_day = 0.0;
  double selectivity = 0.0;  ///< 0 = indiscriminate, 1 = strongly selective.
};

struct Liveweight {
  double liveweight_kg = 0.0;
  double body_condition_score = 0.0;
};

struct Health {
  double vitality = 1.0;         ///< 1 = unaffected, 0 = dead.
  double parasite_burden = 0.0;  ///< Dimensionless index, defined per disease.
};

struct Reproduction {
  bool pregnant = false;
  int days_pregnant = 0;
  int offspring_count = 0;
};

/// Ownership. Wild populations carry no Owned component - which is exactly how
/// one deer species can be both farmed stock and a pest in the same run.
struct Owned {
  std::uint64_t owner_id = 0;
};

/// Work an entity or task demands from the farm's labour budget.
struct Labour {
  double hours_per_day = 0.0;
};

}  // namespace paddock::core
