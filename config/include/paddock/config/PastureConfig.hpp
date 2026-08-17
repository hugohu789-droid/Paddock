// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#pragma once

#include <string>
#include <string_view>

#include <paddock/core/Pasture.hpp>

namespace paddock::config {

/// Reads a sward definition: one grass, one legume, and how they share light.
///
/// ```toml
/// [sward]
/// par_fraction = 0.5
/// decomposition_rate_per_day = 0.02
///
/// [grass]
/// species_id = "ryegrass_perennial"
/// specific_leaf_area_m2_per_kg = 20.0
/// extinction_coefficient = 0.5
/// radiation_use_efficiency_g_per_mj = 1.5
/// base_temperature_c = 4.4
/// optimum_temperature_c = 20.0
/// maximum_temperature_c = 35.0
/// senescence_rate_per_day = 0.02
/// residual_kg_dm_per_ha = 400.0
/// nitrogen_content_fraction = 0.035
///
/// [legume]
/// # ... the same keys, plus:
/// nitrogen_fixation_kg_per_t_dm = 25.0
/// ```
///
/// Adding cocksfoot or lucerne means writing one of these, not writing a class.
[[nodiscard]] core::SwardParameters load_sward(const std::string& path);

[[nodiscard]] core::SwardParameters parse_sward(std::string_view text, const std::string& path);

}  // namespace paddock::config
