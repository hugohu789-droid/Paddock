#pragma once

#include <string_view>

/// Optional LLM features at the edge of the system: natural language to
/// scenario TOML, report writing, result explanation, CSV column mapping.
/// Reads core snapshots; never writes simulation state.
namespace paddock::ai {

[[nodiscard]] std::string_view module_name() noexcept;

}  // namespace paddock::ai
