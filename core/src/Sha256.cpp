#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include <paddock/core/Sha256.hpp>

namespace paddock::core {

namespace {

constexpr std::array<std::uint32_t, 64> kRoundConstants = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U,
    0xab1c5ed5U, 0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU,
    0x9bdc06a7U, 0xc19bf174U, 0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU,
    0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU, 0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
    0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU,
    0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U, 0xa2bfe8a1U, 0xa81a664bU,
    0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U, 0x19a4c116U,
    0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U,
    0xc67178f2U};

constexpr std::uint32_t rotate_right(std::uint32_t value, unsigned int shift) noexcept {
  return (value >> shift) | (value << (32U - shift));
}

constexpr std::uint32_t big_endian_word(const std::uint8_t* bytes) noexcept {
  return (static_cast<std::uint32_t>(bytes[0]) << 24U) |
         (static_cast<std::uint32_t>(bytes[1]) << 16U) |
         (static_cast<std::uint32_t>(bytes[2]) << 8U) | static_cast<std::uint32_t>(bytes[3]);
}

}  // namespace

void Sha256::process_block(const std::uint8_t* block) noexcept {
  std::array<std::uint32_t, 64> schedule{};
  for (std::size_t i = 0; i < 16; ++i) {
    schedule[i] = big_endian_word(block + (i * 4));
  }
  for (std::size_t i = 16; i < 64; ++i) {
    const std::uint32_t previous = schedule[i - 15];
    const std::uint32_t recent = schedule[i - 2];
    const std::uint32_t sigma0 =
        rotate_right(previous, 7) ^ rotate_right(previous, 18) ^ (previous >> 3U);
    const std::uint32_t sigma1 =
        rotate_right(recent, 17) ^ rotate_right(recent, 19) ^ (recent >> 10U);
    schedule[i] = schedule[i - 16] + sigma0 + schedule[i - 7] + sigma1;
  }

  std::array<std::uint32_t, 8> working = state_;
  for (std::size_t round = 0; round < 64; ++round) {
    const std::uint32_t sum1 =
        rotate_right(working[4], 6) ^ rotate_right(working[4], 11) ^ rotate_right(working[4], 25);
    const std::uint32_t choice = (working[4] & working[5]) ^ (~working[4] & working[6]);
    const std::uint32_t temp1 =
        working[7] + sum1 + choice + kRoundConstants[round] + schedule[round];
    const std::uint32_t sum0 =
        rotate_right(working[0], 2) ^ rotate_right(working[0], 13) ^ rotate_right(working[0], 22);
    const std::uint32_t majority =
        (working[0] & working[1]) ^ (working[0] & working[2]) ^ (working[1] & working[2]);
    const std::uint32_t temp2 = sum0 + majority;

    working[7] = working[6];
    working[6] = working[5];
    working[5] = working[4];
    working[4] = working[3] + temp1;
    working[3] = working[2];
    working[2] = working[1];
    working[1] = working[0];
    working[0] = temp1 + temp2;
  }

  for (std::size_t i = 0; i < state_.size(); ++i) {
    state_[i] += working[i];
  }
}

void Sha256::update(const std::uint8_t* data, std::size_t size) noexcept {
  total_bytes_ += size;
  std::size_t offset = 0;

  if (buffered_ > 0) {
    const std::size_t wanted = buffer_.size() - buffered_;
    const std::size_t copied = size < wanted ? size : wanted;
    for (std::size_t i = 0; i < copied; ++i) {
      buffer_[buffered_ + i] = data[i];
    }
    buffered_ += copied;
    offset = copied;
    if (buffered_ < buffer_.size()) {
      return;
    }
    process_block(buffer_.data());
    buffered_ = 0;
  }

  while (offset + buffer_.size() <= size) {
    process_block(data + offset);
    offset += buffer_.size();
  }

  for (std::size_t i = offset; i < size; ++i) {
    buffer_[buffered_] = data[i];
    ++buffered_;
  }
}

void Sha256::update(std::string_view text) noexcept {
  update(reinterpret_cast<const std::uint8_t*>(text.data()), text.size());  // NOLINT
}

Sha256::Digest Sha256::finish() noexcept {
  const std::uint64_t total_bits = total_bytes_ * 8;

  // Padding: a single 1 bit, zeroes, then the message length as 64 big-endian
  // bits, arranged so the total is a whole number of 64-byte blocks.
  constexpr std::uint8_t kPadStart = 0x80U;
  buffer_[buffered_] = kPadStart;
  ++buffered_;
  if (buffered_ > 56) {
    while (buffered_ < buffer_.size()) {
      buffer_[buffered_] = 0;
      ++buffered_;
    }
    process_block(buffer_.data());
    buffered_ = 0;
  }
  while (buffered_ < 56) {
    buffer_[buffered_] = 0;
    ++buffered_;
  }
  for (int shift = 56; shift >= 0; shift -= 8) {
    buffer_[buffered_] =
        static_cast<std::uint8_t>((total_bits >> static_cast<unsigned>(shift)) & 0xffU);
    ++buffered_;
  }
  process_block(buffer_.data());
  buffered_ = 0;

  Digest digest{};
  for (std::size_t i = 0; i < state_.size(); ++i) {
    digest[i * 4] = static_cast<std::uint8_t>((state_[i] >> 24U) & 0xffU);
    digest[(i * 4) + 1] = static_cast<std::uint8_t>((state_[i] >> 16U) & 0xffU);
    digest[(i * 4) + 2] = static_cast<std::uint8_t>((state_[i] >> 8U) & 0xffU);
    digest[(i * 4) + 3] = static_cast<std::uint8_t>(state_[i] & 0xffU);
  }
  return digest;
}

std::string Sha256::to_hex(const Digest& digest) {
  constexpr std::string_view kDigits = "0123456789abcdef";
  std::string hex;
  hex.reserve(digest.size() * 2);
  for (const std::uint8_t byte : digest) {
    hex.push_back(kDigits[byte >> 4U]);
    hex.push_back(kDigits[byte & 0x0fU]);
  }
  return hex;
}

std::string Sha256::hex_finish() {
  return to_hex(finish());
}

std::string Sha256::hex_of(std::string_view text) {
  Sha256 hasher;
  hasher.update(text);
  return hasher.hex_finish();
}

}  // namespace paddock::core
