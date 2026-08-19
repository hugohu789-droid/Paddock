// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#include <stdexcept>
#include <string>
#include <utility>

#include <paddock/core/FarmletGrid.hpp>

namespace paddock::core {

FarmletGrid::FarmletGrid(const Raster<SoilWaterParameters>& soils, const SwardParameters& sward,
                         const FarmletInitialState& initial, double latitude_degrees)
    : cols_(soils.cols()), rows_(soils.rows()), transform_(soils.transform()) {
  latitude_degrees_ = latitude_degrees;
  if (soils.empty()) {
    throw std::invalid_argument("FarmletGrid: the soil raster has no cells");
  }
  cells_.reserve(soils.size());
  for (std::size_t row = 0; row < rows_; ++row) {
    for (std::size_t col = 0; col < cols_; ++col) {
      cells_.emplace_back(soils(col, row), sward, initial, latitude_degrees);
    }
  }
}

const Farmlet& FarmletGrid::cell(std::size_t col, std::size_t row) const {
  if (col >= cols_ || row >= rows_) {
    throw std::out_of_range("FarmletGrid: cell (" + std::to_string(col) + ", " +
                            std::to_string(row) + ") is outside " + std::to_string(cols_) + "x" +
                            std::to_string(rows_));
  }
  return cells_[(row * cols_) + col];
}

void FarmletGrid::set_opening_stocks(BudgetLedger& ledger) const {
  ledger.set_opening_stock(Budget::Water, mean_soil_water_mm());
  ledger.set_opening_stock(Budget::DryMatter, mean_cover_kg_dm());
  ledger.set_opening_stock(Budget::Nitrogen, mean_total_nitrogen_kg());
}

void FarmletGrid::set_terrain(const Topography& ground) {
  if (ground.slope_degrees.cols() != cols_ || ground.slope_degrees.rows() != rows_) {
    throw std::invalid_argument(
        "FarmletGrid::set_terrain: the terrain must have the same shape as the grid");
  }
  radiation_ = std::make_unique<SlopeRadiationTable>(ground, latitude_degrees_);
}

double FarmletGrid::radiation_ratio(std::size_t col, std::size_t row, int day_of_year) const {
  return radiation_ == nullptr ? 1.0 : radiation_->ratio(col, row, day_of_year);
}

void FarmletGrid::step(const DailyWeather& weather, BudgetLedger* ledger,
                       const std::vector<double>& irrigation_mm) {
  const int day = weather.date.day_of_year();

  const auto water_for = [&irrigation_mm](std::size_t index) {
    return index < irrigation_mm.size() ? std::max(0.0, irrigation_mm[index]) : 0.0;
  };

  if (ledger == nullptr) {
    for (std::size_t row = 0; row < rows_; ++row) {
      for (std::size_t col = 0; col < cols_; ++col) {
        const std::size_t index = (row * cols_) + col;
        cells_[index].step(weather, radiation_ratio(col, row, day), nullptr, water_for(index));
      }
    }
    return;
  }

  scratch_.reset();
  for (std::size_t row = 0; row < rows_; ++row) {
    for (std::size_t col = 0; col < cols_; ++col) {
      const std::size_t index = (row * cols_) + col;
      cells_[index].step(weather, radiation_ratio(col, row, day), &scratch_, water_for(index));
    }
  }
  ledger->add_scaled(scratch_, 1.0 / static_cast<double>(cells_.size()));
}

PastureSward::Defoliation FarmletGrid::graze_cell(std::size_t col, std::size_t row,
                                                  double requested_kg_dm_per_ha) {
  if (col >= cols_ || row >= rows_) {
    throw std::out_of_range("FarmletGrid::graze_cell: cell is outside the grid");
  }
  return cells_[(row * cols_) + col].graze(requested_kg_dm_per_ha);
}

template <typename Fn>
Raster<double> FarmletGrid::snapshot(Fn&& value_of) const {
  Raster<double> raster(cols_, rows_, transform_);
  for (std::size_t row = 0; row < rows_; ++row) {
    for (std::size_t col = 0; col < cols_; ++col) {
      raster(col, row) = value_of(cells_[(row * cols_) + col]);
    }
  }
  return raster;
}

Raster<double> FarmletGrid::cover_kg_dm() const {
  return snapshot([](const Farmlet& farmlet) { return farmlet.sward().cover_kg_dm(); });
}

Raster<double> FarmletGrid::depletion_mm() const {
  return snapshot([](const Farmlet& farmlet) { return farmlet.soil().depletion_mm(); });
}

double FarmletGrid::total_available_water_mm() const noexcept {
  return cells_.empty() ? 0.0 : cells_.front().soil().parameters().total_available_water_mm;
}

Raster<double> FarmletGrid::soil_water_mm() const {
  return snapshot([](const Farmlet& farmlet) { return farmlet.soil().water_mm(); });
}

Raster<double> FarmletGrid::water_stress() const {
  return snapshot([](const Farmlet& farmlet) { return farmlet.soil().stress_coefficient(); });
}

Raster<double> FarmletGrid::legume_fraction() const {
  return snapshot([](const Farmlet& farmlet) { return farmlet.sward().legume_fraction(); });
}

namespace {

template <typename Fn>
double compensated_mean(const std::vector<Farmlet>& cells, Fn&& value_of) {
  KahanSum sum;
  for (const Farmlet& farmlet : cells) {
    sum.add(value_of(farmlet));
  }
  return cells.empty() ? 0.0 : sum.value() / static_cast<double>(cells.size());
}

}  // namespace

double FarmletGrid::mean_cover_kg_dm() const {
  return compensated_mean(cells_,
                          [](const Farmlet& farmlet) { return farmlet.sward().cover_kg_dm(); });
}

double FarmletGrid::mean_soil_water_mm() const {
  return compensated_mean(cells_, [](const Farmlet& farmlet) { return farmlet.soil().water_mm(); });
}

double FarmletGrid::mean_total_nitrogen_kg() const {
  return compensated_mean(
      cells_, [](const Farmlet& farmlet) { return farmlet.sward().total_nitrogen_kg(); });
}

}  // namespace paddock::core
