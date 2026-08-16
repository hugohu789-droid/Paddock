#include <gtest/gtest.h>

#include <string>

#include <paddock/core/Version.hpp>

namespace paddock::core {
namespace {

TEST(VersionTest, EngineVersionIsDottedAndMatchesTheConstants) {
  const std::string version = engine_version();

  EXPECT_EQ(version, std::to_string(kVersionMajor) + "." + std::to_string(kVersionMinor) + "." +
                         std::to_string(kVersionPatch));
}

}  // namespace
}  // namespace paddock::core
