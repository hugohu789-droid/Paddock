// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#pragma once

#include <initializer_list>
#include <string>
#include <string_view>
#include <toml++/toml.hpp>

#include <paddock/config/ConfigError.hpp>

/// Internal helpers shared by the loaders. toml++ appears here and in the
/// loaders' translation units, and nowhere else in the repository.
namespace paddock::config::detail {

/// Throws a ConfigError pointing at a node's own position in the file.
[[noreturn]] void throw_at(const toml::node& node, const std::string& path,
                           const std::string& detail);

/// Throws pointing at a table, used when something is missing rather than wrong.
[[noreturn]] void throw_in(const toml::table& table, const std::string& path,
                           const std::string& detail);

toml::table parse_text(std::string_view text, const std::string& path);
toml::table parse_file(const std::string& path);

const toml::table& require_table(const toml::table& parent, std::string_view key,
                                 const std::string& path);

double require_double(const toml::table& table, std::string_view key, const std::string& path);
std::string require_string(const toml::table& table, std::string_view key, const std::string& path);

/// Missing is allowed; present but wrong is not.
double optional_double(const toml::table& table, std::string_view key, double fallback,
                       const std::string& path);
std::string optional_string(const toml::table& table, std::string_view key,
                            const std::string& fallback);

[[nodiscard]] bool has(const toml::table& table, std::string_view key);

/// Rejects any key that is not in `allowed`.
///
/// A silently ignored key is how a farm ends up simulated with a default
/// nobody chose: `runoff_fration = 0.05` would otherwise parse, validate and
/// run, and the farm would simply never shed water.
void reject_unknown_keys(const toml::table& table, std::initializer_list<std::string_view> allowed,
                         const std::string& path, const std::string& context);

/// Turns a core validation message into a ConfigError pointing at the table
/// that supplied the values, so the two validation layers report the same way.
void require_valid(const std::string& validation_error, const toml::table& table,
                   const std::string& path);

}  // namespace paddock::config::detail
