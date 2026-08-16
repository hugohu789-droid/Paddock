#pragma once

#include <string>

namespace paddock::core {

inline constexpr int kVersionMajor = 0;
inline constexpr int kVersionMinor = 1;
inline constexpr int kVersionPatch = 0;

/// Engine version, recorded in every scenario bundle. A bundle only reproduces
/// bit-for-bit on the engine version that produced it.
std::string engine_version();

}  // namespace paddock::core
