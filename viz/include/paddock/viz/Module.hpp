#pragma once

#include <string_view>

/// Visualisation: 2D map view and 3D terrain view built from core snapshots.
/// Reads simulation state, never writes it.
namespace paddock::viz {

[[nodiscard]] std::string_view module_name() noexcept;

}  // namespace paddock::viz
