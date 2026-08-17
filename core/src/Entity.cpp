// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#include <algorithm>

#include <paddock/core/Entity.hpp>

namespace paddock::core {

EntityId World::create() {
  const EntityId id{next_id_};
  ++next_id_;
  // IDs are handed out in ascending order, so a push_back keeps alive_ sorted.
  alive_.push_back(id);
  return id;
}

bool World::destroy(EntityId id) {
  const auto position = std::lower_bound(alive_.begin(), alive_.end(), id);
  if (position == alive_.end() || *position != id) {
    return false;
  }
  alive_.erase(position);
  // Store order is irrelevant here: each erase is independent of the others.
  for (auto& entry : stores_) {
    entry.second->erase(id);
  }
  return true;
}

bool World::alive(EntityId id) const noexcept {
  return std::binary_search(alive_.begin(), alive_.end(), id);
}

}  // namespace paddock::core
