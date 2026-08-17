// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#pragma once

#include <string_view>

/// Optional LLM features at the edge of the system: natural language to
/// scenario TOML, report writing, result explanation, CSV column mapping.
/// Reads core snapshots; never writes simulation state.
namespace paddock::ai {

[[nodiscard]] std::string_view module_name() noexcept;

}  // namespace paddock::ai
