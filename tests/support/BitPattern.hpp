#pragma once

#include <cstdint>
#include <cstring>
#include <iterator>
#include <vector>

/// Helpers for the determinism assertions.
///
/// "Same seed, same bits" has to be checked on the bit patterns themselves, but
/// memcmp over an array of doubles is the wrong tool: it has no idea that -0.0
/// and 0.0 compare equal as values while differing as bits, and static analysis
/// rightly objects to it. Converting each value to its bit pattern first says
/// what is meant, and gives a readable diff when an assertion fails.
namespace paddock::test_support {

[[nodiscard]] inline std::uint64_t bits_of(double value) noexcept {
  std::uint64_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
}

template <typename Range>
[[nodiscard]] std::vector<std::uint64_t> bit_patterns(const Range& values) {
  std::vector<std::uint64_t> bits;
  bits.reserve(static_cast<std::size_t>(std::size(values)));
  for (const double value : values) {
    bits.push_back(bits_of(value));
  }
  return bits;
}

}  // namespace paddock::test_support
