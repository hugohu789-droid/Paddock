// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#include <string>
#include <utility>

#include <paddock/core/DataSource.hpp>

namespace paddock::core {

ConnectionStatus ConnectionStatus::available(std::string detail) {
  return ConnectionStatus{true, std::move(detail)};
}

ConnectionStatus ConnectionStatus::unavailable(std::string actionable_error) {
  return ConnectionStatus{false, std::move(actionable_error)};
}

}  // namespace paddock::core
