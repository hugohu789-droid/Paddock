// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#pragma once

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

/// Reads a calibration CSV from data/calibration/ into named columns.
///
/// General where CalibrationSeries is monthly, because the livestock tables are
/// grids rather than series. Lines starting with '#' are comments, and the
/// first line that is not is the header.
namespace paddock::test {

class CalibrationTable {
 public:
  explicit CalibrationTable(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
      throw std::runtime_error("cannot open calibration table " + path);
    }

    std::string line;
    while (std::getline(file, line)) {
      if (line.empty() || line.front() == '#') {
        continue;
      }
      std::vector<std::string> fields;
      std::stringstream row(line);
      std::string field;
      while (std::getline(row, field, ',')) {
        fields.push_back(field);
      }
      if (columns_.empty()) {
        columns_ = std::move(fields);
      } else {
        rows_.push_back(std::move(fields));
      }
    }
    if (columns_.empty()) {
      throw std::runtime_error("calibration table has no header: " + path);
    }
  }

  [[nodiscard]] std::size_t size() const noexcept { return rows_.size(); }

  /// A cell as text. Throws for a column this table does not have, rather than
  /// returning an empty string a caller would read as a zero.
  [[nodiscard]] const std::string& text(std::size_t row, const std::string& column) const {
    for (std::size_t i = 0; i < columns_.size(); ++i) {
      if (columns_[i] == column) {
        if (i >= rows_.at(row).size()) {
          throw std::runtime_error("row " + std::to_string(row) + " has no '" + column + "'");
        }
        return rows_.at(row)[i];
      }
    }
    throw std::runtime_error("no column '" + column + "' in this table");
  }

  [[nodiscard]] double number(std::size_t row, const std::string& column) const {
    return std::stod(text(row, column));
  }

 private:
  std::vector<std::string> columns_;
  std::vector<std::vector<std::string>> rows_;
};

}  // namespace paddock::test
