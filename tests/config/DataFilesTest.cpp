// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

// The definitions committed under data/ have to keep loading.
//
// A configuration format is only an interface if the files that ship with the
// project are held to it. Without this suite a schema change would break every
// example silently, and the first person to notice would be someone trying the
// simulator for the first time.

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

#include <paddock/config/FarmConfig.hpp>
#include <paddock/config/PastureConfig.hpp>
#include <paddock/config/SoilConfig.hpp>
#include <paddock/config/WeatherConfig.hpp>

namespace paddock::config {
namespace {

std::string data_path(const std::string& relative) {
  return std::string(PADDOCK_DATA_DIR) + "/" + relative;
}

TEST(DataFilesTest, TheExampleSoilLoads) {
  const SoilDefinition soil = load_soil(data_path("soils/templeton-silt-loam-example.toml"));

  EXPECT_EQ(soil.name, "templeton_silt_loam_example");
  // FAO-56 Eq. 82 on the measurements in the file.
  EXPECT_DOUBLE_EQ(soil.water.total_available_water_mm, 120.0);
  EXPECT_TRUE(soil.water.validation_error().empty());
}

TEST(DataFilesTest, TheExampleSwardLoads) {
  const core::SwardParameters sward =
      load_sward(data_path("pastures/ryegrass-clover-example.toml"));

  EXPECT_EQ(sward.grass.species_id, "ryegrass_perennial");
  EXPECT_EQ(sward.legume.species_id, "clover_white");
  EXPECT_GT(sward.legume.nitrogen_fixation_kg_per_t_dm, 0.0);
  EXPECT_TRUE(sward.validation_error().empty());
}

TEST(DataFilesTest, TheExampleWeatherSiteLoads) {
  const core::SyntheticWeatherParameters site =
      load_synthetic_weather(data_path("weather/canterbury-plains-example.toml"));

  EXPECT_EQ(site.site_name, "canterbury_plains_example");
  EXPECT_LT(site.latitude_degrees, 0.0) << "a New Zealand site is south of the equator";
  EXPECT_TRUE(site.validation_error().empty());
  // The shape of a Southern Hemisphere year: January warmer than July.
  EXPECT_GT(site.months[0].mean_daily_max_c, site.months[6].mean_daily_max_c);
}

// The farm set is discovered rather than enumerated. This test deliberately
// does NOT name the three farms that ship today: asserting a count or a list
// here would reintroduce exactly the fixed set the directory is meant to
// replace, and adding a farm would then require editing a test.
TEST(DataFilesTest, EveryFarmDescriptionLoads) {
  const std::vector<FarmDefinition> farms = load_farms(data_path("farms"));

  ASSERT_FALSE(farms.empty()) << "data/farms/ has no farm descriptions";
  for (const FarmDefinition& farm : farms) {
    EXPECT_TRUE(farm.validation_error().empty()) << farm.name << ": " << farm.validation_error();
    EXPECT_FALSE(farm.region.empty()) << farm.name;
    EXPECT_LT(farm.location.latitude_degrees, 0.0) << farm.name << " is not in New Zealand";
    // A farm whose location is not surveyed has to say what its coordinates
    // actually are. The flag is only useful if the unset case is explained.
    if (!farm.location.location_verified) {
      EXPECT_FALSE(farm.location.source.empty())
          << farm.name << " has an unverified location and no note saying what it is";
    }
  }
}

TEST(DataFilesTest, FarmNamesAreUnique) {
  const std::vector<FarmDefinition> farms = load_farms(data_path("farms"));

  std::vector<std::string> names;
  names.reserve(farms.size());
  for (const FarmDefinition& farm : farms) {
    names.push_back(farm.name);
  }
  const std::vector<std::string> sorted = names;
  EXPECT_TRUE(std::is_sorted(sorted.begin(), sorted.end()))
      << "load_farms should return farms in a stable order";
  EXPECT_EQ(std::unique(names.begin(), names.end()), names.end());
}

// Massey publishes Dairy 4's effective area and its paddock count, so this one
// farm can be checked against its source rather than against itself. The
// generated outline is a rectangle and is not claimed to be the farm's shape;
// what is claimed is that the area and the paddock size come from Massey.
//
// Validation, not a regression pin: the figures are on Massey's farm page,
// quoted in data/farms/massey-dairy-4.toml.
TEST(DataFilesTest, MasseyDairy4MatchesItsPublishedAreaAndPaddockCount) {
  const FarmDefinition farm = load_farm(data_path("farms/massey-dairy-4.toml"));

  ASSERT_TRUE(farm.stated_effective_hectares.has_value());
  EXPECT_DOUBLE_EQ(farm.stated_effective_hectares.value(), 221.0);

  // The declared extent has to reproduce the published area, or the file is
  // describing a different farm from the one it cites.
  EXPECT_NEAR(farm.boundary_hectares(), farm.stated_effective_hectares.value(), 1e-6);

  // "approximately 80 x 1.5-3.5 hectare paddocks all with race access".
  //
  // The window is 70 to 90 rather than exactly 80 because "approximately" is
  // Massey's word, not a hedge added here: the generator tiles whole paddocks
  // into a rectangle and lands where the arithmetic puts it. On this extent it
  // lands on 80, and a change that moved it outside the window would mean this
  // farm no longer reproduces the subdivision it cites.
  const std::vector<core::Paddock> paddocks = farm.make_paddocks();
  EXPECT_GE(paddocks.size(), 70U) << "generated " << paddocks.size();
  EXPECT_LE(paddocks.size(), 90U) << "generated " << paddocks.size();

  for (const core::Paddock& paddock : paddocks) {
    EXPECT_GE(paddock.area_hectares(), 1.5) << paddock.name;
    EXPECT_LE(paddock.area_hectares(), 3.5) << paddock.name;
  }

  // The mean follows from the two published figures - 221 / 80 = 2.7625 ha -
  // so it is a check on the pair rather than a third assertion.
  const double mean_hectares = farm.boundary_hectares() / static_cast<double>(paddocks.size());
  EXPECT_NEAR(mean_hectares, 221.0 / 80.0, 0.05) << "mean paddock " << mean_hectares << " ha";

  GTEST_LOG_(INFO) << paddocks.size() << " paddocks averaging " << mean_hectares << " ha, over "
                   << farm.boundary_hectares() << " ha";
}

}  // namespace
}  // namespace paddock::config
