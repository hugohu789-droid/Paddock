#include "TomlSupport.hpp"

#include <algorithm>
#include <fstream>
#include <initializer_list>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace paddock::config::detail {

namespace {

std::string quoted(std::string_view key) {
  return "'" + std::string(key) + "'";
}

std::string type_name(toml::node_type type) {
  switch (type) {
    case toml::node_type::table:
      return "a table";
    case toml::node_type::array:
      return "an array";
    case toml::node_type::string:
      return "a string";
    case toml::node_type::integer:
      return "an integer";
    case toml::node_type::floating_point:
      return "a number";
    case toml::node_type::boolean:
      return "a boolean";
    case toml::node_type::date:
    case toml::node_type::time:
    case toml::node_type::date_time:
      return "a date or time";
    default:
      return "nothing";
  }
}

}  // namespace

void throw_at(const toml::node& node, const std::string& path, const std::string& detail) {
  throw ConfigError(path, node.source().begin.line, node.source().begin.column, detail);
}

void throw_in(const toml::table& table, const std::string& path, const std::string& detail) {
  throw ConfigError(path, table.source().begin.line, table.source().begin.column, detail);
}

toml::table parse_text(std::string_view text, const std::string& path) {
  try {
    return toml::parse(text, path);
  } catch (const toml::parse_error& error) {
    throw ConfigError(path, error.source().begin.line, error.source().begin.column,
                      std::string(error.description()));
  }
}

toml::table parse_file(const std::string& path) {
  const std::ifstream file(path, std::ios::binary);
  if (!file) {
    // Line 1 rather than 0: the message still reads as a location, and nothing
    // downstream has to special-case a file that was never opened.
    throw ConfigError(path, 1, 1, "cannot open configuration file");
  }
  std::ostringstream contents;
  contents << file.rdbuf();
  return parse_text(contents.str(), path);
}

const toml::table& require_table(const toml::table& parent, std::string_view key,
                                 const std::string& path) {
  const toml::node* node = parent.get(key);
  if (node == nullptr) {
    throw_in(parent, path, "missing required table " + quoted(key));
  }
  if (!node->is_table()) {
    throw_at(*node, path, quoted(key) + " must be a table, found " + type_name(node->type()));
  }
  return *node->as_table();
}

double require_double(const toml::table& table, std::string_view key, const std::string& path) {
  const toml::node* node = table.get(key);
  if (node == nullptr) {
    throw_in(table, path, "missing required key " + quoted(key));
  }
  // Integers are accepted where a number is wanted: writing `runoff = 0` is
  // natural, and rejecting it would be pedantry rather than validation.
  if (node->is_floating_point()) {
    return node->as_floating_point()->get();
  }
  if (node->is_integer()) {
    return static_cast<double>(node->as_integer()->get());
  }
  throw_at(*node, path, quoted(key) + " must be a number, found " + type_name(node->type()));
}

std::string require_string(const toml::table& table, std::string_view key,
                           const std::string& path) {
  const toml::node* node = table.get(key);
  if (node == nullptr) {
    throw_in(table, path, "missing required key " + quoted(key));
  }
  if (!node->is_string()) {
    throw_at(*node, path, quoted(key) + " must be a string, found " + type_name(node->type()));
  }
  return node->as_string()->get();
}

double optional_double(const toml::table& table, std::string_view key, double fallback,
                       const std::string& path) {
  if (table.get(key) == nullptr) {
    return fallback;
  }
  return require_double(table, key, path);
}

std::string optional_string(const toml::table& table, std::string_view key,
                            const std::string& fallback) {
  const toml::node* node = table.get(key);
  if (node == nullptr || !node->is_string()) {
    return fallback;
  }
  return node->as_string()->get();
}

bool has(const toml::table& table, std::string_view key) {
  return table.get(key) != nullptr;
}

void reject_unknown_keys(const toml::table& table, std::initializer_list<std::string_view> allowed,
                         const std::string& path, const std::string& context) {
  for (const auto& [key, value] : table) {
    const std::string_view name = key.str();
    if (std::find(allowed.begin(), allowed.end(), name) != allowed.end()) {
      continue;
    }
    std::string known;
    for (const std::string_view candidate : allowed) {
      if (!known.empty()) {
        known += ", ";
      }
      known.append(candidate);
    }
    std::string message = "unknown key ";
    message.append(quoted(name)).append(" in ").append(context);
    message.append(". Known keys are: ").append(known);
    throw_at(value, path, message);
  }
}

void require_valid(const std::string& validation_error, const toml::table& table,
                   const std::string& path) {
  if (!validation_error.empty()) {
    throw_in(table, path, validation_error);
  }
}

}  // namespace paddock::config::detail
