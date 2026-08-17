// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace paddock::core {

/// The three quantities every process in the simulator must account for.
///
/// Units are fixed per budget and stated here once: dry matter in kg DM, water
/// in mm of depth over the modelled area, nitrogen in kg N. Every new process
/// declares which of these lines it touches.
enum class Budget : std::uint8_t { DryMatter, Water, Nitrogen };

inline constexpr std::size_t kBudgetCount = 3;

/// The conservation suite's tolerance. A closed 365-day run must balance to
/// this; it is never widened to make a build pass.
inline constexpr double kConservationTolerance = 1e-9;

[[nodiscard]] std::string_view budget_name(Budget budget) noexcept;
[[nodiscard]] std::string_view budget_unit(Budget budget) noexcept;

/// Compensated summation (Kahan-Babuska-Neumaier).
///
/// A year of daily flows is ~365 additions per process; naive summation of
/// values spanning several orders of magnitude drifts well past 1e-9, which
/// would make the conservation gate a test of floating-point luck rather than
/// of the model.
class KahanSum {
 public:
  void add(double value) noexcept;

  [[nodiscard]] double value() const noexcept { return sum_ + compensation_; }

  void reset() noexcept;

 private:
  double sum_ = 0.0;
  double compensation_ = 0.0;
};

/// One process's contribution to one budget, kept for diagnostics: when a
/// budget fails to close, the offending process should be obvious.
///
/// Internal transfers are counted separately from the flows that cross the
/// system boundary. Folding them into `inflow` and `outflow` would make the
/// entry unreplayable, and replaying entries is how a per-cell ledger merges
/// into a whole-farm one.
struct ProcessEntry {
  std::string process;
  double inflow = 0.0;
  double outflow = 0.0;
  double internal = 0.0;
};

/// Tracks what entered and left each budget over a run.
///
/// The ledger does not know what a paddock or an animal is. Processes report
/// their flows to it, and the conservation tests assert that opening stock plus
/// inflows minus outflows equals the closing stock the model actually holds.
class BudgetLedger {
 public:
  void set_opening_stock(Budget budget, double amount);

  /// Amounts must be non-negative: direction is carried by the call, not by the
  /// sign, so a mis-signed flow cannot silently cancel out.
  void record_inflow(Budget budget, std::string_view process, double amount);
  void record_outflow(Budget budget, std::string_view process, double amount);

  /// A move between two pools inside the system - grazed pasture becoming
  /// animal intake, say. It changes no total, and is recorded for diagnostics
  /// only, so that a closed budget still shows where matter went.
  void record_internal_transfer(Budget budget, std::string_view process, double amount);

  [[nodiscard]] double opening_stock(Budget budget) const;
  [[nodiscard]] double total_inflow(Budget budget) const;
  [[nodiscard]] double total_outflow(Budget budget) const;

  /// What the model should be holding now.
  [[nodiscard]] double expected_closing_stock(Budget budget) const;

  /// Observed minus expected. Zero means the budget closes.
  [[nodiscard]] double residual(Budget budget, double observed_closing_stock) const;

  [[nodiscard]] bool closes(Budget budget, double observed_closing_stock,
                            double tolerance = kConservationTolerance) const;

  /// Entries in the order processes first reported to this budget.
  [[nodiscard]] const std::vector<ProcessEntry>& entries(Budget budget) const;

  /// Adds another ledger's flows, every amount multiplied by `factor`.
  ///
  /// This is how a spatially explicit run accounts for itself: each cell
  /// reports to a scratch ledger, and the day's scratch is folded into the
  /// farm's ledger once, scaled by one over the number of cells, so the farm
  /// ledger holds per-hectare means. Entries merge in the other ledger's own
  /// order, which is fixed, so the result does not depend on traversal - the
  /// same property that would let partitions be merged if this ever ran in
  /// parallel.
  ///
  /// Opening stocks are not touched: they belong to the ledger that holds them.
  void add_scaled(const BudgetLedger& other, double factor);

  /// Human-readable breakdown, used in conservation failure messages.
  [[nodiscard]] std::string report(Budget budget, double observed_closing_stock) const;

  void reset();

 private:
  struct BudgetState {
    double opening_stock = 0.0;
    KahanSum inflow;
    KahanSum outflow;
    std::vector<ProcessEntry> entries;
  };

  [[nodiscard]] static std::size_t index_of(Budget budget) noexcept;
  [[nodiscard]] BudgetState& state(Budget budget);
  [[nodiscard]] const BudgetState& state(Budget budget) const;
  [[nodiscard]] static ProcessEntry& entry_for(BudgetState& budget_state, std::string_view process);

  std::array<BudgetState, kBudgetCount> budgets_;
};

}  // namespace paddock::core
