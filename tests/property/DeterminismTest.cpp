// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

// Determinism is a property of the whole engine, not of one type: the same seed
// must produce bit-identical output, and no traversal order may be able to
// change a result. Both are asserted here against the RNG and entity machinery
// that every later subsystem will draw from.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <numeric>
#include <random>
#include <vector>

#include <paddock/core/Components.hpp>
#include <paddock/core/Distributions.hpp>
#include <paddock/core/Entity.hpp>
#include <paddock/core/Rng.hpp>
#include <paddock/core/SimulationClock.hpp>

#include "support/BitPattern.hpp"

namespace paddock::core {
namespace {

using test_support::bit_patterns;

constexpr std::size_t kHerdSize = 40;
constexpr int kSimulatedDays = 90;

/// A stand-in for a daily per-animal process. It is deliberately not a model of
/// anything: what is under test is that identity-keyed streams and sorted
/// component storage make the result independent of how the herd is traversed.
std::vector<double> run_herd(std::uint64_t master_seed, const std::vector<std::size_t>& order) {
  World world;
  const RngStreams streams(master_seed);

  std::vector<EntityId> herd;
  herd.reserve(kHerdSize);
  for (std::size_t i = 0; i < kHerdSize; ++i) {
    const EntityId animal = world.create();
    world.add(animal, Liveweight{50.0 + static_cast<double>(i), 3.0});
    herd.push_back(animal);
  }

  // One engine per animal, seeded from its identity. Engines are created once
  // and advanced across days: re-seeding daily would repeat the same draw.
  std::vector<std::mt19937_64> engines;
  engines.reserve(herd.size());
  for (const EntityId animal : herd) {
    engines.push_back(streams.stream_for(Subsystem::Livestock, animal));
  }

  SimulationClock clock(Date{2023, 7, 1});
  for (int day = 0; day < kSimulatedDays; ++day) {
    for (const std::size_t index : order) {
      world.get<Liveweight>(herd[index])->liveweight_kg += uniform(engines[index], -0.2, 0.6);
    }
    clock.advance();
  }

  std::vector<double> liveweights;
  liveweights.reserve(herd.size());
  world.store<Liveweight>().for_each([&liveweights](EntityId /*id*/, const Liveweight& weight) {
    liveweights.push_back(weight.liveweight_kg);
  });
  return liveweights;
}

std::vector<std::size_t> ascending_order() {
  std::vector<std::size_t> order(kHerdSize);
  std::iota(order.begin(), order.end(), 0U);
  return order;
}

// Bit-level equality, not near-equality: "same seed, same bits" is the claim.
TEST(DeterminismTest, SameSeedGivesBitIdenticalResults) {
  const std::vector<double> first = run_herd(20240701, ascending_order());
  const std::vector<double> second = run_herd(20240701, ascending_order());

  EXPECT_EQ(bit_patterns(first), bit_patterns(second));
}

TEST(DeterminismTest, DifferentSeedsGiveDifferentResults) {
  const std::vector<double> first = run_herd(20240701, ascending_order());
  const std::vector<double> second = run_herd(20240702, ascending_order());

  EXPECT_NE(bit_patterns(first), bit_patterns(second));
}

// The guard against a future parallel or reordered traversal: draws are keyed
// by entity ID, so shuffling the work queue cannot move a single bit.
TEST(DeterminismTest, TraversalOrderCannotChangeResults) {
  std::vector<std::size_t> shuffled = ascending_order();
  std::mt19937_64 shuffler(99);  // shuffles the test's work queue, not the model
  std::shuffle(shuffled.begin(), shuffled.end(), shuffler);
  std::vector<std::size_t> reversed = ascending_order();
  std::reverse(reversed.begin(), reversed.end());

  const std::vector<double> in_order = run_herd(20240701, ascending_order());

  EXPECT_EQ(bit_patterns(in_order), bit_patterns(run_herd(20240701, shuffled)));
  EXPECT_EQ(bit_patterns(in_order), bit_patterns(run_herd(20240701, reversed)));
}

TEST(DeterminismTest, ComponentIterationIsSortedByIdNotByCreationOrder) {
  World world;
  const EntityId first = world.create();
  const EntityId second = world.create();
  const EntityId third = world.create();

  world.add(third, Liveweight{3.0, 3.0});
  world.add(first, Liveweight{1.0, 3.0});
  world.add(second, Liveweight{2.0, 3.0});

  std::vector<double> seen;
  world.store<Liveweight>().for_each(
      [&seen](EntityId /*id*/, const Liveweight& weight) { seen.push_back(weight.liveweight_kg); });

  EXPECT_EQ(seen, (std::vector<double>{1.0, 2.0, 3.0}));
}

}  // namespace
}  // namespace paddock::core
