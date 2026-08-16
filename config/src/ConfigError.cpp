#include <cstddef>
#include <string>
#include <utility>

#include <paddock/config/ConfigError.hpp>

namespace paddock::config {

namespace {

std::string format(const std::string& path, std::size_t line, std::size_t column,
                   const std::string& detail) {
  return path + ':' + std::to_string(line) + ':' + std::to_string(column) + ": " + detail;
}

}  // namespace

ConfigError::ConfigError(std::string path, std::size_t line, std::size_t column,
                         const std::string& detail)
    : std::runtime_error(format(path, line, column, detail)),
      path_(std::move(path)),
      line_(line),
      column_(column),
      detail_(detail) {}

}  // namespace paddock::config
