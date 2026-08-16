#include <gtest/gtest.h>

#include <cstddef>
#include <string>
#include <string_view>

#include <paddock/core/Sha256.hpp>

namespace paddock::core {
namespace {

// The published FIPS 180-4 / NIST test vectors. A hash implementation that has
// not been checked against them is a hash implementation that silently differs
// from `sha256sum`, which would make every recorded provenance hash useless.
TEST(Sha256Test, MatchesTheNistVectors) {
  EXPECT_EQ(Sha256::hex_of(""), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  EXPECT_EQ(Sha256::hex_of("abc"),
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  EXPECT_EQ(Sha256::hex_of("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
            "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST(Sha256Test, MatchesTheLongVector) {
  Sha256 hasher;
  const std::string block(1000, 'a');
  for (int i = 0; i < 1000; ++i) {
    hasher.update(block);
  }

  EXPECT_EQ(hasher.hex_finish(),
            "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

// The 56-byte vector sits exactly on the boundary where padding needs a second
// block, and snapshots arrive in whatever chunks a stream hands over, so the
// buffering has to be right for every split.
TEST(Sha256Test, ChunkedUpdatesMatchASingleUpdate) {
  const std::string text =
      "date,rainfall_mm,min_air_temperature_c,max_air_temperature_c\n"
      "2023-07-01,4.5,2.1,11.8\n2023-07-02,0.0,-1.2,9.4\n";
  const std::string expected = Sha256::hex_of(text);

  for (const std::size_t chunk :
       {std::size_t{1}, std::size_t{7}, std::size_t{55}, std::size_t{56}, std::size_t{63},
        std::size_t{64}, std::size_t{65}, std::size_t{200}}) {
    Sha256 hasher;
    for (std::size_t offset = 0; offset < text.size(); offset += chunk) {
      hasher.update(std::string_view(text).substr(offset, chunk));
    }
    EXPECT_EQ(hasher.hex_finish(), expected) << "chunk size " << chunk;
  }
}

TEST(Sha256Test, DifferentContentGivesADifferentDigest) {
  EXPECT_NE(Sha256::hex_of("2023-07-01,4.5"), Sha256::hex_of("2023-07-01,4.6"));
}

}  // namespace
}  // namespace paddock::core
