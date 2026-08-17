// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#include <string_view>

#include <paddock/gis/Module.hpp>

namespace paddock::gis {

std::string_view module_name() noexcept {
  return "gis";
}

}  // namespace paddock::gis
