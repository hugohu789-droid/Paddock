#pragma once

#include <cstdint>
#include <random>
#include <string_view>

#include <paddock/core/Entity.hpp>

namespace paddock::core {

/// One stream per subsystem. Values are fixed and must never be renumbered:
/// they are part of the reproducibility contract of every scenario bundle.
enum class Subsystem : std::uint64_t {
  Weather = 1,
  SoilWater = 2,
  PastureGrowth = 3,
  Livestock = 4,
  Disease = 5,
  Pest = 6,
  Management = 7,
  Scenario = 8
};

[[nodiscard]] std::string_view subsystem_name(Subsystem subsystem) noexcept;

/// Mixes a master seed, a subsystem and a key into a stream seed.
///
/// The key is an entity ID or another stable identifier — never a loop counter.
/// Keying by identity is what lets a future parallel or reordered traversal
/// produce bit-identical results.
[[nodiscard]] std::uint64_t derive_seed(std::uint64_t master_seed, Subsystem subsystem,
                                        std::uint64_t key = 0) noexcept;

/// Hands out the engines a run is allowed to use. There is no global engine and
/// no default seed: every draw traces back to the master seed in the bundle.
class RngStreams {
 public:
  explicit RngStreams(std::uint64_t master_seed) noexcept : master_seed_(master_seed) {}

  [[nodiscard]] std::uint64_t master_seed() const noexcept { return master_seed_; }

  /// Engine for a subsystem's own draws (weather, market noise).
  [[nodiscard]] std::mt19937_64 stream(Subsystem subsystem) const noexcept;

  /// Engine for one entity within a subsystem. Two entities never share a
  /// stream, and an entity's stream does not depend on when it was created.
  [[nodiscard]] std::mt19937_64 stream_for(Subsystem subsystem, EntityId id) const noexcept;

 private:
  std::uint64_t master_seed_;
};

}  // namespace paddock::core
