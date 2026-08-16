#include <gtest/gtest.h>

#include <cstdint>
#include <random>
#include <set>
#include <vector>

#include <paddock/core/Entity.hpp>
#include <paddock/core/Rng.hpp>

namespace paddock::core {
namespace {

constexpr std::uint64_t kMasterSeed = 20240701;

TEST(DeriveSeedTest, IsAPureFunctionOfItsArguments) {
  EXPECT_EQ(derive_seed(kMasterSeed, Subsystem::Weather, 7),
            derive_seed(kMasterSeed, Subsystem::Weather, 7));
}

TEST(DeriveSeedTest, NeighbouringInputsGiveUnrelatedSeeds) {
  std::set<std::uint64_t> seeds;
  for (std::uint64_t key = 0; key < 64; ++key) {
    seeds.insert(derive_seed(kMasterSeed, Subsystem::Livestock, key));
    seeds.insert(derive_seed(kMasterSeed + 1, Subsystem::Livestock, key));
  }

  EXPECT_EQ(seeds.size(), 128U) << "seed derivation collided";
  EXPECT_NE(derive_seed(kMasterSeed, Subsystem::Weather, 1),
            derive_seed(kMasterSeed, Subsystem::Disease, 1));
}

TEST(RngStreamsTest, EachSubsystemGetsItsOwnStream) {
  const RngStreams streams(kMasterSeed);
  auto weather = streams.stream(Subsystem::Weather);
  auto disease = streams.stream(Subsystem::Disease);

  EXPECT_EQ(streams.master_seed(), kMasterSeed);
  EXPECT_NE(weather(), disease());
}

TEST(RngStreamsTest, EachEntityGetsItsOwnStream) {
  const RngStreams streams(kMasterSeed);
  std::set<std::uint64_t> first_draws;

  for (std::uint64_t id = 1; id <= 100; ++id) {
    auto engine = streams.stream_for(Subsystem::Livestock, EntityId{id});
    first_draws.insert(engine());
  }

  EXPECT_EQ(first_draws.size(), 100U);
}

// The engine must not be able to reach a global generator. A stream handed out
// twice is the same stream, and drawing from one copy cannot disturb the other.
TEST(RngStreamsTest, StreamsAreIndependentCopies) {
  const RngStreams streams(kMasterSeed);
  auto first = streams.stream_for(Subsystem::Pest, EntityId{42});
  auto second = streams.stream_for(Subsystem::Pest, EntityId{42});

  constexpr int kDraws = 5;
  std::vector<std::uint64_t> from_first;
  from_first.reserve(kDraws);
  for (int draw = 0; draw < kDraws; ++draw) {
    from_first.push_back(first());
  }
  std::vector<std::uint64_t> from_second;
  from_second.reserve(kDraws);
  for (int draw = 0; draw < kDraws; ++draw) {
    from_second.push_back(second());
  }

  EXPECT_EQ(from_first, from_second);
}

TEST(RngStreamsTest, SubsystemNamesAreStable) {
  EXPECT_EQ(subsystem_name(Subsystem::PastureGrowth), "pasture_growth");
  EXPECT_EQ(subsystem_name(Subsystem::Weather), "weather");
  EXPECT_EQ(static_cast<std::uint64_t>(Subsystem::Weather), 1U);
}

}  // namespace
}  // namespace paddock::core
