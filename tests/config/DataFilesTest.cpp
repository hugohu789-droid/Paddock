// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

// The definitions committed under data/ have to keep loading.
//
// A configuration format is only an interface if the files that ship with the
// project are held to it. Without this suite a schema change would break every
// example silently, and the first person to notice would be someone trying the
// simulator for the first time.

#include <gtest/gtest.h>

#include <string>

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

}  // namespace
}  // namespace paddock::config
