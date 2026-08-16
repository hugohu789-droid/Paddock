#include <stdexcept>
#include <string>
#include <utility>

#include <paddock/core/FarmletGrid.hpp>

namespace paddock::core {

FarmletGrid::FarmletGrid(const Raster<SoilWaterParameters>& soils, const SwardParameters& sward,
                         const FarmletInitialState& initial, double latitude_degrees)
    : cols_(soils.cols()), rows_(soils.rows()), transform_(soils.transform()) {
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

void FarmletGrid::step(const DailyWeather& weather, BudgetLedger* ledger) {
  if (ledger == nullptr) {
    for (Farmlet& farmlet : cells_) {
      farmlet.step(weather);
    }
    return;
  }

  scratch_.reset();
  for (Farmlet& farmlet : cells_) {
    farmlet.step(weather, &scratch_);
  }
  ledger->add_scaled(scratch_, 1.0 / static_cast<double>(cells_.size()));
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
