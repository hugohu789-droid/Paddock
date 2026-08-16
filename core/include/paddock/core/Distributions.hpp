#pragma once

#include <cstdint>
#include <random>

namespace paddock::core {

/// Distributions with algorithms fixed by this file rather than by whichever
/// standard library the binary was built against.
///
/// `std::uniform_real_distribution` and `std::normal_distribution` are
/// implementation-defined: libstdc++, libc++ and the MSVC STL use different
/// algorithms and return different numbers from the same engine and the same
/// seed. That makes cross-platform reproducibility impossible to claim, and a
/// golden regression baseline pinned on Linux meaningless on Windows.
///
/// Everything here consumes `std::mt19937_64`, whose output sequence *is*
/// specified by the standard, and turns it into deviates by a stated algorithm.
/// See docs/adr/0007-own-distributions.md for the limits of the guarantee.

/// A uniform deviate in [0, 1).
///
/// Takes the top 53 bits of one engine draw and scales by 2^-53: every
/// operation is exact in IEEE-754 double, so this is bit-identical everywhere.
[[nodiscard]] double uniform_unit(std::mt19937_64& engine) noexcept;

/// A uniform deviate in [low, high). Returns `low` when the range is empty.
[[nodiscard]] double uniform(std::mt19937_64& engine, double low, double high) noexcept;

/// A uniform integer in [low, high], unbiased by rejection sampling.
[[nodiscard]] std::uint64_t uniform_int(std::mt19937_64& engine, std::uint64_t low,
                                        std::uint64_t high) noexcept;

/// True with probability `probability`, clamped to [0, 1].
[[nodiscard]] bool bernoulli(std::mt19937_64& engine, double probability) noexcept;

/// A standard normal deviate by the Marsaglia polar method.
///
/// The polar method produces deviates in pairs. This function discards the
/// second rather than caching it: a cached deviate is hidden state, and hidden
/// state is exactly what makes a run stop being reproducible when the order of
/// calls changes. One wasted draw per call is a good trade.
[[nodiscard]] double standard_normal(std::mt19937_64& engine) noexcept;

[[nodiscard]] double normal(std::mt19937_64& engine, double mean,
                            double standard_deviation) noexcept;

/// An exponential deviate with the given mean, by inversion.
[[nodiscard]] double exponential(std::mt19937_64& engine, double mean) noexcept;

/// A gamma deviate (shape > 0, scale > 0) by Marsaglia and Tsang's method.
/// Daily rainfall depth on wet days is conventionally modelled as gamma, so
/// this is the shape the weather generator needs.
[[nodiscard]] double gamma(std::mt19937_64& engine, double shape, double scale) noexcept;

}  // namespace paddock::core
