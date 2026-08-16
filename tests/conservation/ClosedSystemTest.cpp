// The conservation gate. A closed synthetic farm run for 365 simulated days
// must balance dry matter, water and nitrogen to within 1e-9.
//
// M1 has no growth, drainage or intake model yet, so the pools here move matter
// around according to a random schedule rather than according to agronomy. What
// is under test is the accounting machinery every later process must report
// through: the ledger, its compensated summation, and the rule that nothing
// crosses the system boundary without being recorded. When M2 adds real
// processes they plug into this same harness and the tolerance does not move.

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <random>
#include <string>
#include <vector>

#include <paddock/core/BudgetLedger.hpp>
#include <paddock/core/Distributions.hpp>
#include <paddock/core/Entity.hpp>
#include <paddock/core/Rng.hpp>
#include <paddock/core/SimulationClock.hpp>

#include "support/BitPattern.hpp"

namespace paddock::core {
namespace {

using test_support::bit_patterns;

constexpr int kDaysInYear = 365;
constexpr std::uint64_t kMasterSeed = 20240701;

/// A quantity of one budget held somewhere in the farm — pasture cover, soil
/// water, animal body reserves. Test-local by design: the store is generic, so
/// a component type does not have to live in core to be usable.
struct Pool {
  double amount = 0.0;
};

constexpr std::array<Budget, kBudgetCount> kAllBudgets = {Budget::DryMatter, Budget::Water,
                                                          Budget::Nitrogen};

std::string budget_label(Budget budget) {
  return std::string(budget_name(budget));
}

/// Three pools per budget, so matter has somewhere to move to.
struct Farm {
  World world;
  std::array<std::vector<EntityId>, kBudgetCount> pools;
  std::vector<std::mt19937_64> engines;
  BudgetLedger ledger;

  explicit Farm(std::uint64_t master_seed) {
    const RngStreams streams(master_seed);
    for (std::size_t budget_index = 0; budget_index < kBudgetCount; ++budget_index) {
      for (int pool = 0; pool < 3; ++pool) {
        const EntityId id = world.create();
        world.add(id, Pool{1000.0 + (100.0 * static_cast<double>(pool))});
        pools[budget_index].push_back(id);
        engines.push_back(streams.stream_for(Subsystem::PastureGrowth, id));
      }
    }
    for (const Budget budget : kAllBudgets) {
      ledger.set_opening_stock(budget, total(budget));
    }
  }

  /// Compensated, for the same reason the ledger is: a year of small daily
  /// flows on top of a large stock must not lose precision in the assertion
  /// itself.
  [[nodiscard]] double total(Budget budget) const {
    KahanSum sum;
    for (const EntityId id : pools[static_cast<std::size_t>(budget)]) {
      sum.add(world.get<Pool>(id)->amount);
    }
    return sum.value();
  }

  [[nodiscard]] static std::size_t engine_index(Budget budget, std::size_t pool) {
    return (static_cast<std::size_t>(budget) * 3) + pool;
  }
};

/// Moves matter between pools without letting any cross the boundary.
void step_internal_transfers(Farm& farm, Budget budget) {
  const std::vector<EntityId>& pools = farm.pools[static_cast<std::size_t>(budget)];
  for (std::size_t source = 0; source < pools.size(); ++source) {
    auto& engine = farm.engines[Farm::engine_index(budget, source)];
    Pool& from = *farm.world.get<Pool>(pools[source]);
    const double moved = from.amount * uniform(engine, 0.0, 0.05);
    Pool& to = *farm.world.get<Pool>(pools[(source + 1) % pools.size()]);
    from.amount -= moved;
    to.amount += moved;
    farm.ledger.record_internal_transfer(budget, "redistribution", moved);
  }
}

/// Boundary flows: matter genuinely entering or leaving the farm, each reported
/// to the ledger. An unreported flow is exactly the bug this gate exists for.
void step_boundary_flows(Farm& farm, Budget budget, bool report_the_outflow) {
  const std::vector<EntityId>& pools = farm.pools[static_cast<std::size_t>(budget)];
  auto& engine = farm.engines[Farm::engine_index(budget, 0)];

  const double inflow = uniform(engine, 0.0, 5.0);
  farm.world.get<Pool>(pools.front())->amount += inflow;
  farm.ledger.record_inflow(budget, "boundary_inflow", inflow);

  const double outflow = uniform(engine, 0.0, 5.0);
  farm.world.get<Pool>(pools.back())->amount -= outflow;
  if (report_the_outflow) {
    farm.ledger.record_outflow(budget, "boundary_outflow", outflow);
  }
}

TEST(ConservationTest, ClosedFarmBalancesForAYear) {
  Farm farm(kMasterSeed);
  SimulationClock clock(Date{2023, 7, 1});

  for (int day = 0; day < kDaysInYear; ++day) {
    for (const Budget budget : kAllBudgets) {
      step_internal_transfers(farm, budget);
    }
    clock.advance();
  }

  ASSERT_EQ(clock.day_index(), kDaysInYear);
  ASSERT_EQ(clock.date(), (Date{2024, 6, 30}));
  for (const Budget budget : kAllBudgets) {
    const double observed = farm.total(budget);
    EXPECT_TRUE(farm.ledger.closes(budget, observed)) << farm.ledger.report(budget, observed);
    // Nothing crossed the boundary, so the year cannot have changed the total.
    EXPECT_NEAR(observed, farm.ledger.opening_stock(budget), kConservationTolerance)
        << budget_label(budget);
  }
}

TEST(ConservationTest, BoundaryFlowsBalanceWhenTheyAreReported) {
  Farm farm(kMasterSeed);
  SimulationClock clock(Date{2023, 7, 1});

  for (int day = 0; day < kDaysInYear; ++day) {
    for (const Budget budget : kAllBudgets) {
      step_internal_transfers(farm, budget);
      step_boundary_flows(farm, budget, /*report_the_outflow=*/true);
    }
    clock.advance();
  }

  for (const Budget budget : kAllBudgets) {
    const double observed = farm.total(budget);
    EXPECT_TRUE(farm.ledger.closes(budget, observed)) << farm.ledger.report(budget, observed);
    EXPECT_GT(farm.ledger.total_inflow(budget), 0.0);
    EXPECT_GT(farm.ledger.total_outflow(budget), 0.0);
  }
}

// Negative control: a gate that cannot fail proves nothing. One unreported
// outflow must break the budget by more than the tolerance.
TEST(ConservationTest, AnUnreportedOutflowIsDetected) {
  Farm farm(kMasterSeed);

  for (int day = 0; day < kDaysInYear; ++day) {
    for (const Budget budget : kAllBudgets) {
      step_boundary_flows(farm, budget, /*report_the_outflow=*/false);
    }
  }

  for (const Budget budget : kAllBudgets) {
    const double observed = farm.total(budget);
    EXPECT_FALSE(farm.ledger.closes(budget, observed)) << budget_label(budget);
    EXPECT_LT(farm.ledger.residual(budget, observed), -1.0) << budget_label(budget);
  }
}

TEST(ConservationTest, AYearOfAccountingIsReproducibleBitForBit) {
  const auto run_year = [](std::uint64_t seed) {
    Farm farm(seed);
    for (int day = 0; day < kDaysInYear; ++day) {
      for (const Budget budget : kAllBudgets) {
        step_internal_transfers(farm, budget);
        step_boundary_flows(farm, budget, /*report_the_outflow=*/true);
      }
    }
    std::array<double, kBudgetCount> totals{};
    for (std::size_t i = 0; i < kBudgetCount; ++i) {
      totals[i] = farm.total(kAllBudgets[i]);
    }
    return totals;
  };

  const std::array<double, kBudgetCount> first = run_year(kMasterSeed);
  const std::array<double, kBudgetCount> second = run_year(kMasterSeed);
  const std::array<double, kBudgetCount> other_seed = run_year(kMasterSeed + 1);

  EXPECT_EQ(bit_patterns(first), bit_patterns(second));
  EXPECT_NE(bit_patterns(first), bit_patterns(other_seed));
}

}  // namespace
}  // namespace paddock::core
