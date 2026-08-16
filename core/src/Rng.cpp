#include <cstdint>
#include <random>
#include <string_view>

#include <paddock/core/Rng.hpp>

namespace paddock::core {

namespace {

/// splitmix64 (Vigna, public domain). Used purely as a mixing function so that
/// nearby seeds — master seed 1 and 2, entity 7 and 8 — produce unrelated
/// streams. Deterministic on every platform: unsigned arithmetic only.
constexpr std::uint64_t splitmix64(std::uint64_t state) noexcept {
  std::uint64_t result = state + 0x9E3779B97F4A7C15ULL;
  result = (result ^ (result >> 30U)) * 0xBF58476D1CE4E5B9ULL;
  result = (result ^ (result >> 27U)) * 0x94D049BB133111EBULL;
  return result ^ (result >> 31U);
}

}  // namespace

std::string_view subsystem_name(Subsystem subsystem) noexcept {
  switch (subsystem) {
    case Subsystem::Weather:
      return "weather";
    case Subsystem::SoilWater:
      return "soil_water";
    case Subsystem::PastureGrowth:
      return "pasture_growth";
    case Subsystem::Livestock:
      return "livestock";
    case Subsystem::Disease:
      return "disease";
    case Subsystem::Pest:
      return "pest";
    case Subsystem::Management:
      return "management";
    case Subsystem::Scenario:
      return "scenario";
  }
  return "unknown";
}

std::uint64_t derive_seed(std::uint64_t master_seed, Subsystem subsystem,
                          std::uint64_t key) noexcept {
  const std::uint64_t subsystem_bits = static_cast<std::uint64_t>(subsystem);
  return splitmix64(splitmix64(master_seed ^ splitmix64(subsystem_bits)) ^ splitmix64(key));
}

std::mt19937_64 RngStreams::stream(Subsystem subsystem) const noexcept {
  return std::mt19937_64(derive_seed(master_seed_, subsystem));
}

std::mt19937_64 RngStreams::stream_for(Subsystem subsystem, EntityId id) const noexcept {
  return std::mt19937_64(derive_seed(master_seed_, subsystem, value_of(id)));
}

}  // namespace paddock::core
