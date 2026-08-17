// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

// Scenario bundles, and the promise they make: the same bundle reproduces the
// same run, and a bundle whose inputs have moved says so instead of running.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <paddock/config/ConfigError.hpp>
#include <paddock/config/ScenarioConfig.hpp>
#include <paddock/core/Simulation.hpp>

#include "support/BitPattern.hpp"

namespace paddock::config {
namespace {

using test_support::bit_patterns;

std::string shipped_bundle() {
  return std::string(PADDOCK_DATA_DIR) + "/scenarios/canterbury-baseline";
}

/// A copy of the shipped bundle in a temporary directory, removed on
/// destruction. Tests that tamper with a bundle tamper with this, never with
/// the committed files.
class BundleCopy {
 public:
  BundleCopy() {
    directory_ =
        std::filesystem::temp_directory_path() /
        ("paddock-bundle-" +
         std::to_string(std::filesystem::hash_value(std::filesystem::path(shipped_bundle()))) +
         "-" + testing::UnitTest::GetInstance()->current_test_info()->name());
    std::filesystem::remove_all(directory_);
    std::filesystem::copy(shipped_bundle(), directory_, std::filesystem::copy_options::recursive);
  }

  BundleCopy(const BundleCopy&) = delete;
  BundleCopy& operator=(const BundleCopy&) = delete;
  BundleCopy(BundleCopy&&) = delete;
  BundleCopy& operator=(BundleCopy&&) = delete;

  ~BundleCopy() {
    std::error_code ignored;
    std::filesystem::remove_all(directory_, ignored);
  }

  [[nodiscard]] std::string path() const { return directory_.string(); }

  /// Replaces `before` with `after` in one of the bundle's files.
  void edit(const std::string& file, const std::string& before, const std::string& after) const {
    const std::filesystem::path target = directory_ / file;
    std::string text;
    {
      const std::ifstream input(target, std::ios::binary);
      std::ostringstream buffer;
      buffer << input.rdbuf();
      text = buffer.str();
    }
    const std::string::size_type position = text.find(before);
    ASSERT_NE(position, std::string::npos) << "'" << before << "' not found in " << file;
    text.replace(position, before.size(), after);
    std::ofstream output(target, std::ios::binary);
    output << text;
  }

 private:
  std::filesystem::path directory_;
};

std::vector<double> cover_series(const core::RunResult& result) {
  std::vector<double> cover;
  cover.reserve(result.daily.size());
  for (const core::DailyRecord& day : result.daily) {
    cover.push_back(day.cover_kg_dm);
  }
  return cover;
}

std::vector<double> run_bundle(const std::string& path) {
  const ScenarioBundle bundle = load_scenario(path);
  core::Farmlet farmlet = bundle.make_farmlet();
  return cover_series(core::run(farmlet, *bundle.weather, bundle.range));
}

TEST(ScenarioBundleTest, TheShippedBundleLoadsWithItsHashesIntact) {
  const ScenarioBundle bundle = load_scenario(shipped_bundle());

  EXPECT_EQ(bundle.name, "canterbury_baseline");
  EXPECT_EQ(bundle.master_seed, 20240701U);
  EXPECT_EQ(bundle.range.first, (core::Date{2023, 7, 1}));
  EXPECT_EQ(bundle.range.last, (core::Date{2024, 6, 30}));
  EXPECT_DOUBLE_EQ(bundle.latitude_degrees, -43.5);
  EXPECT_EQ(bundle.inputs.size(), 3U);
  EXPECT_TRUE(bundle.inputs_unchanged());
  for (const BundleInput& input : bundle.inputs) {
    EXPECT_EQ(input.recorded_sha256.size(), 64U) << input.relative_path;
    EXPECT_EQ(input.recorded_sha256, input.actual_sha256) << input.relative_path;
  }
}

TEST(ScenarioBundleTest, TheBundleRunsAndItsBudgetsClose) {
  const ScenarioBundle bundle = load_scenario(shipped_bundle());
  core::Farmlet farmlet = bundle.make_farmlet();

  const core::RunResult result = core::run(farmlet, *bundle.weather, bundle.range);

  // 2024 is a leap year, so the farm year is 366 days long.
  EXPECT_EQ(result.daily.size(), 366U);
  EXPECT_TRUE(result.budgets_close(farmlet));
  EXPECT_GT(result.summary.total_growth_kg_dm, 0.0);
  EXPECT_GT(result.summary.total_rainfall_mm, 0.0);
  EXPECT_EQ(result.weather_provenance.source_name, "synthetic");
}

// The whole point of a bundle. Loading and running it twice gives the same
// numbers to the last bit - on this platform and this engine version, which is
// exactly what the manifest pins.
TEST(ScenarioBundleTest, RunningTheSameBundleTwiceIsBitIdentical) {
  EXPECT_EQ(bit_patterns(run_bundle(shipped_bundle())), bit_patterns(run_bundle(shipped_bundle())));
}

TEST(ScenarioBundleTest, AnotherSeedIsAnotherRun) {
  const BundleCopy copy;
  // The manifest is not itself hashed, so changing the seed leaves the input
  // hashes intact and only the run changes.
  copy.edit("scenario.toml", "master_seed = 20240701", "master_seed = 20240702");

  EXPECT_NE(bit_patterns(run_bundle(shipped_bundle())), bit_patterns(run_bundle(copy.path())));
}

// A bundle is only reproducible if its inputs are the ones it was built on.
TEST(ScenarioBundleTest, AChangedInputIsRefusedAndNamed) {
  const BundleCopy copy;
  // Valid TOML and a plausible edit: exactly the kind that would otherwise go
  // unnoticed and quietly change every result that followed.
  copy.edit("soil.toml", "runoff_fraction = 0.05", "runoff_fraction = 0.15");

  try {
    static_cast<void>(load_scenario(copy.path()));
    FAIL() << "expected the changed input to be refused";
  } catch (const ConfigError& error) {
    const std::string message = error.what();
    EXPECT_NE(message.find("soil.toml"), std::string::npos) << message;
    EXPECT_NE(message.find("recorded"), std::string::npos) << message;
    EXPECT_NE(message.find("actual"), std::string::npos) << message;
  }
}

TEST(ScenarioBundleTest, AnEngineVersionMismatchIsRefused) {
  const BundleCopy copy;
  copy.edit("scenario.toml", "engine_version = \"0.1.0\"", "engine_version = \"0.0.9\"");

  try {
    static_cast<void>(load_scenario(copy.path()));
    FAIL() << "expected the version mismatch to be refused";
  } catch (const ConfigError& error) {
    const std::string message = error.what();
    EXPECT_NE(message.find("0.0.9"), std::string::npos) << message;
    EXPECT_NE(message.find("Re-record"), std::string::npos) << message;
  }
}

TEST(ScenarioBundleTest, LoadingUncheckedIgnoresTheHashesAndTheVersion) {
  // The tool that writes the hashes has to be able to read a bundle that does
  // not have them yet; nothing else may.
  const BundleCopy copy;
  copy.edit("soil.toml", "runoff_fraction = 0.05", "runoff_fraction = 0.15");

  const ScenarioBundle bundle = load_scenario_unchecked(copy.path());

  EXPECT_EQ(bundle.name, "canterbury_baseline");
  EXPECT_FALSE(bundle.inputs_unchanged());
  EXPECT_EQ(bundle.changed_inputs().size(), 1U);
}

TEST(ScenarioBundleTest, AMissingBundleIsReportedWithItsPath) {
  try {
    static_cast<void>(load_scenario(std::string(PADDOCK_DATA_DIR) + "/scenarios/not-a-bundle"));
    FAIL() << "expected the missing bundle to be refused";
  } catch (const ConfigError& error) {
    EXPECT_NE(std::string(error.what()).find("scenario.toml"), std::string::npos);
    EXPECT_NE(std::string(error.what()).find("cannot open"), std::string::npos);
  }
}

TEST(ScenarioBundleTest, AnUnknownWeatherKindIsRefused) {
  const BundleCopy copy;
  copy.edit("scenario.toml", "kind = \"synthetic\"", "kind = \"cliflo_live\"");

  try {
    static_cast<void>(load_scenario_unchecked(copy.path()));
    FAIL() << "expected the unknown weather kind to be refused";
  } catch (const ConfigError& error) {
    EXPECT_NE(std::string(error.what()).find("cliflo_live"), std::string::npos);
    EXPECT_NE(std::string(error.what()).find("synthetic, snapshot"), std::string::npos);
  }
}

}  // namespace
}  // namespace paddock::config
