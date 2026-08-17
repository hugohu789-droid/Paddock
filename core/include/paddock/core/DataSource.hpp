// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#pragma once

#include <string>

/// The vocabulary every data source shares.
///
/// CLAUDE.md describes one port - describe(), test_connection(), fetch(query) -
/// wrapped around every source: weather, elevation, cadastre, waterways, soils,
/// land cover. These two types are the parts of it that do not depend on what
/// is being fetched, and they lived in Weather.hpp only because weather was
/// first. Elevation and cadastre need them too, and neither should have to
/// include a weather header to say what its licence is.
namespace paddock::core {

/// What a data source is, in the terms a user needs before trusting it.
struct SourceDescription {
  std::string name;
  std::string licence;
  std::string coverage;  ///< Spatial and temporal extent, in plain words
  std::string cadence;   ///< How often new data appears
};

/// The result of `test_connection`: either usable, or an error a user can act
/// on. "Failed" is never an acceptable message on its own - say what is missing
/// and what to do about it.
struct ConnectionStatus {
  bool ok = false;
  std::string message;

  [[nodiscard]] static ConnectionStatus available(std::string detail);
  [[nodiscard]] static ConnectionStatus unavailable(std::string actionable_error);
};

}  // namespace paddock::core
