// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

#include <paddock/config/Provenance.hpp>

#include "TomlSupport.hpp"

namespace paddock::config {

std::string to_string(Provenance status) {
  switch (status) {
    case Provenance::Direct:
      return "direct";
    case Provenance::Derived:
      return "derived";
    case Provenance::Verify:
      return "verify";
    case Provenance::Placeholder:
      return "placeholder";
  }
  return "unknown";
}

bool provenance_from_string(std::string_view text, Provenance& out) {
  if (text == "direct") {
    out = Provenance::Direct;
    return true;
  }
  if (text == "derived") {
    out = Provenance::Derived;
    return true;
  }
  if (text == "verify") {
    out = Provenance::Verify;
    return true;
  }
  if (text == "placeholder") {
    out = Provenance::Placeholder;
    return true;
  }
  return false;
}

std::string SourcedValue::validation_error(const std::string& context) const {
  if (is_evidence() && source_id.empty()) {
    return context + " is marked '" + to_string(status) +
           "' but cites nothing. A value that rests on a source has to name it";
  }
  return {};
}

std::vector<std::string> load_source_ids(const std::string& manifest_path) {
  const toml::table root = detail::parse_file(manifest_path);

  const toml::node* sources_node = root.get("source");
  if (sources_node == nullptr) {
    throw ConfigError(manifest_path, 1, 1, "no [source.*] entries in this manifest");
  }
  const toml::table* sources = sources_node->as_table();
  if (sources == nullptr) {
    throw ConfigError(manifest_path, 1, 1, "'source' must be a table of sources");
  }

  std::vector<std::string> ids;
  ids.reserve(sources->size());
  for (const auto& [key, value] : *sources) {
    static_cast<void>(value);
    ids.emplace_back(key.str());
  }
  std::sort(ids.begin(), ids.end());
  return ids;
}

}  // namespace paddock::config
