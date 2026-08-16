#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>

namespace paddock::config {

/// A configuration problem, with the place in the file that caused it.
///
/// The message reads `path:line:column: what is wrong`, the form every editor
/// and terminal already knows how to jump to. A validated file format is the
/// v1 interface to this simulator, so an error that only says "invalid
/// configuration" would be the whole user interface failing.
class ConfigError : public std::runtime_error {
 public:
  ConfigError(std::string path, std::size_t line, std::size_t column, const std::string& detail);

  [[nodiscard]] const std::string& path() const noexcept { return path_; }

  [[nodiscard]] std::size_t line() const noexcept { return line_; }

  [[nodiscard]] std::size_t column() const noexcept { return column_; }

  [[nodiscard]] const std::string& detail() const noexcept { return detail_; }

 private:
  std::string path_;
  std::size_t line_ = 0;
  std::size_t column_ = 0;
  std::string detail_;
};

}  // namespace paddock::config
