// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace paddock::core {

/// SHA-256 (FIPS 180-4), implemented here because core takes no dependencies.
///
/// It is used for data snapshot content hashes and scenario bundle
/// fingerprints. The choice of SHA-256 over a cheaper non-cryptographic hash is
/// deliberate: the fetch scripts that download LINZ and NIWA data record hashes
/// with `sha256sum`, and a reviewer must be able to check a bundle's provenance
/// with ordinary command line tools rather than with this program.
class Sha256 {
 public:
  static constexpr std::size_t kDigestBytes = 32;
  using Digest = std::array<std::uint8_t, kDigestBytes>;

  Sha256() = default;

  void update(const std::uint8_t* data, std::size_t size) noexcept;
  void update(std::string_view text) noexcept;

  /// Finalises the hash. The object must not be updated afterwards.
  [[nodiscard]] Digest finish() noexcept;

  /// Lower-case hexadecimal, the form `sha256sum` prints.
  [[nodiscard]] std::string hex_finish();

  [[nodiscard]] static std::string hex_of(std::string_view text);
  [[nodiscard]] static std::string to_hex(const Digest& digest);

 private:
  void process_block(const std::uint8_t* block) noexcept;

  std::array<std::uint32_t, 8> state_ = {0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
                                         0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U};
  std::array<std::uint8_t, 64> buffer_{};
  std::size_t buffered_ = 0;
  std::uint64_t total_bytes_ = 0;
};

}  // namespace paddock::core
