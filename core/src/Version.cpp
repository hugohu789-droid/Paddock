// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#include <string>

#include <paddock/core/Version.hpp>

namespace paddock::core {

std::string engine_version() {
  return std::to_string(kVersionMajor) + '.' + std::to_string(kVersionMinor) + '.' +
         std::to_string(kVersionPatch);
}

}  // namespace paddock::core
