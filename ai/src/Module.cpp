// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#include <string_view>

#include <paddock/ai/Module.hpp>

namespace paddock::ai {

std::string_view module_name() noexcept {
  return "ai";
}

}  // namespace paddock::ai
