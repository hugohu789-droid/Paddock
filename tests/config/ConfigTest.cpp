// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#include <gtest/gtest.h>

#include <string>

#include <paddock/config/ConfigError.hpp>
#include <paddock/config/PastureConfig.hpp>
#include <paddock/config/SoilConfig.hpp>
#include <paddock/config/WeatherConfig.hpp>

namespace paddock::config {
namespace {

const std::string kPath = "data/soils/test.toml";

std::string soil_text(const std::string& water_block) {
  return "[soil]\nname = \"test_soil\"\n\n[water]\n" + water_block;
}

const std::string kValidWaterBlock =
    "total_available_water_mm = 120.0\n"
    "depletion_fraction = 0.6\n"
    "crop_coefficient = 0.95\n"
    "runoff_fraction = 0.05\n";

std::string sward_text(const std::string& legume_extra = "nitrogen_fixation_kg_per_t_dm = 25.0\n") {
  return "[sward]\npar_fraction = 0.5\ndecomposition_rate_per_day = 0.02\n\n"
         "[grass]\nspecies_id = \"ryegrass_perennial\"\n"
         "specific_leaf_area_m2_per_kg = 20.0\nextinction_coefficient = 0.5\n"
         "radiation_use_efficiency_g_per_mj = 1.5\nbase_temperature_c = 4.4\n"
         "optimum_temperature_c = 20.0\nmaximum_temperature_c = 35.0\n"
         "senescence_rate_per_day = 0.02\nresidual_kg_dm_per_ha = 400.0\n"
         "nitrogen_content_fraction = 0.035\n\n"
         "[legume]\nspecies_id = \"clover_white\"\n"
         "specific_leaf_area_m2_per_kg = 25.0\nextinction_coefficient = 0.6\n"
         "radiation_use_efficiency_g_per_mj = 1.4\nbase_temperature_c = 5.0\n"
         "optimum_temperature_c = 22.0\nmaximum_temperature_c = 35.0\n"
         "senescence_rate_per_day = 0.025\nresidual_kg_dm_per_ha = 100.0\n"
         "nitrogen_content_fraction = 0.045\n" +
         legume_extra;
}

std::string weather_text(int month_count = 12, const std::string& site_extra = "") {
  std::string text =
      "[site]\nname = \"test_site\"\nlicence = \"test fixture\"\nlatitude_degrees = -43.5\n" +
      site_extra +
      "\n[variation]\ndaily_temperature_sd_c = 2.0\nradiation_variation_fraction = 0.1\n"
      "wet_day_radiation_fraction = 0.5\nwind_variation_fraction = 0.2\n";
  for (int month = 0; month < month_count; ++month) {
    text +=
        "\n[[month]]\nmean_daily_max_c = 16.0\nmean_daily_min_c = 6.0\n"
        "wet_day_probability = 0.25\nmean_wet_day_rainfall_mm = 8.0\nrainfall_shape = 0.8\n"
        "mean_solar_radiation_mj = 14.0\nmean_wind_speed_m_per_s = 3.0\n";
  }
  return text;
}

/// Fails the test unless the error names the file, a line, and the given text.
void expect_error_mentions(const std::string& text, const std::string& expected,
                           const std::string& path = kPath) {
  try {
    static_cast<void>(parse_soil(text, path));
    FAIL() << "expected the configuration to be rejected";
  } catch (const ConfigError& error) {
    EXPECT_EQ(error.path(), path);
    EXPECT_GT(error.line(), 0U);
    EXPECT_NE(std::string(error.what()).find(expected), std::string::npos)
        << "actual message: " << error.what();
    EXPECT_NE(std::string(error.what()).find(path + ":"), std::string::npos);
  }
}

TEST(SoilConfigTest, ReadsAValidDefinition) {
  const SoilDefinition soil = parse_soil(soil_text(kValidWaterBlock), kPath);

  EXPECT_EQ(soil.name, "test_soil");
  EXPECT_DOUBLE_EQ(soil.water.total_available_water_mm, 120.0);
  EXPECT_DOUBLE_EQ(soil.water.depletion_fraction, 0.6);
  EXPECT_DOUBLE_EQ(soil.water.crop_coefficient, 0.95);
  EXPECT_DOUBLE_EQ(soil.water.runoff_fraction, 0.05);
}

// FAO-56 Eq. 82, which is the form S-map's measurements arrive in.
TEST(SoilConfigTest, ComputesAvailableWaterFromMeasurements) {
  const SoilDefinition soil = parse_soil(soil_text("field_capacity_fraction = 0.38\n"
                                                   "wilting_point_fraction = 0.18\n"
                                                   "rooting_depth_m = 0.6\n"
                                                   "depletion_fraction = 0.6\n"
                                                   "crop_coefficient = 0.95\n"
                                                   "runoff_fraction = 0.05\n"),
                                         kPath);

  EXPECT_DOUBLE_EQ(soil.water.total_available_water_mm, 120.0);
}

TEST(SoilConfigTest, GivingBothFormsOrNeitherIsRejected) {
  expect_error_mentions(soil_text("total_available_water_mm = 120.0\n"
                                  "field_capacity_fraction = 0.38\n"
                                  "wilting_point_fraction = 0.18\n"
                                  "rooting_depth_m = 0.6\n"
                                  "depletion_fraction = 0.6\ncrop_coefficient = 0.95\n"
                                  "runoff_fraction = 0.05\n"),
                        "not both");
  expect_error_mentions(soil_text("depletion_fraction = 0.6\ncrop_coefficient = 0.95\n"
                                  "runoff_fraction = 0.05\n"),
                        "missing available water");
}

TEST(SoilConfigTest, AMissingKeyIsNamed) {
  expect_error_mentions(soil_text("total_available_water_mm = 120.0\n"
                                  "depletion_fraction = 0.6\nrunoff_fraction = 0.05\n"),
                        "missing required key 'crop_coefficient'");
}

TEST(SoilConfigTest, AWrongTypeSaysWhatItFound) {
  expect_error_mentions(soil_text("total_available_water_mm = \"lots\"\n"
                                  "depletion_fraction = 0.6\ncrop_coefficient = 0.95\n"
                                  "runoff_fraction = 0.05\n"),
                        "must be a number, found a string");
}

// The most expensive kind of configuration bug: a typo that parses, validates
// and runs, leaving the farm quietly using a default nobody chose.
TEST(SoilConfigTest, AMisspelledKeyIsRejectedRatherThanIgnored) {
  expect_error_mentions(soil_text("total_available_water_mm = 120.0\n"
                                  "depletion_fraction = 0.6\ncrop_coefficient = 0.95\n"
                                  "runoff_fration = 0.05\n"),
                        "unknown key 'runoff_fration'");
  expect_error_mentions(soil_text("total_available_water_mm = 120.0\n"
                                  "depletion_fraction = 0.6\ncrop_coefficient = 0.95\n"
                                  "runoff_fration = 0.05\n"),
                        "Known keys are:");
}

// The core type's own validation, reported with a place in the file rather than
// as an exception from the middle of a run.
TEST(SoilConfigTest, OutOfRangeValuesAreReportedWithTheirLocation) {
  expect_error_mentions(soil_text("total_available_water_mm = 120.0\n"
                                  "depletion_fraction = 1.4\ncrop_coefficient = 0.95\n"
                                  "runoff_fraction = 0.05\n"),
                        "depletion_fraction must be between 0 and 1");
  expect_error_mentions(soil_text("field_capacity_fraction = 0.18\n"
                                  "wilting_point_fraction = 0.38\n"
                                  "rooting_depth_m = 0.6\n"
                                  "depletion_fraction = 0.6\ncrop_coefficient = 0.95\n"
                                  "runoff_fraction = 0.05\n"),
                        "'wilting_point_fraction' must be below");
}

TEST(SoilConfigTest, MalformedTomlPointsAtTheLine) {
  try {
    static_cast<void>(parse_soil("[soil]\nname = \"test\"\n\n[water\n", kPath));
    FAIL() << "expected the file to be rejected";
  } catch (const ConfigError& error) {
    EXPECT_EQ(error.line(), 4U);
  }
}

TEST(SoilConfigTest, AMissingFileIsReportedAsSuch) {
  try {
    static_cast<void>(load_soil("data/soils/definitely-not-here.toml"));
    FAIL() << "expected the file to be rejected";
  } catch (const ConfigError& error) {
    EXPECT_NE(std::string(error.what()).find("cannot open"), std::string::npos);
    EXPECT_NE(std::string(error.what()).find("definitely-not-here.toml"), std::string::npos);
  }
}

TEST(WeatherConfigTest, ReadsASiteWithTwelveMonths) {
  const core::SyntheticWeatherParameters parameters =
      parse_synthetic_weather(weather_text(), "data/weather/test.toml");

  EXPECT_EQ(parameters.site_name, "test_site");
  EXPECT_DOUBLE_EQ(parameters.latitude_degrees, -43.5);
  EXPECT_DOUBLE_EQ(parameters.months[0].mean_daily_max_c, 16.0);
  EXPECT_DOUBLE_EQ(parameters.months[11].mean_wind_speed_m_per_s, 3.0);
  EXPECT_TRUE(parameters.validation_error().empty());
}

TEST(WeatherConfigTest, TheWrongNumberOfMonthsIsRejected) {
  for (const int months : {0, 11, 13}) {
    try {
      static_cast<void>(parse_synthetic_weather(weather_text(months), "data/weather/test.toml"));
      FAIL() << "expected " << months << " months to be rejected";
    } catch (const ConfigError& error) {
      EXPECT_NE(std::string(error.what()).find("twelve"), std::string::npos);
    }
  }
}

TEST(WeatherConfigTest, AnUnknownKeyInASiteIsRejected) {
  try {
    static_cast<void>(parse_synthetic_weather(weather_text(12, "elevation_m = 30.0\n"),
                                              "data/weather/test.toml"));
    FAIL() << "expected the unknown key to be rejected";
  } catch (const ConfigError& error) {
    EXPECT_NE(std::string(error.what()).find("elevation_m"), std::string::npos);
  }
}

TEST(PastureConfigTest, ReadsASwardAndDefaultsFixationToZero) {
  const core::SwardParameters sward = parse_sward(sward_text(), "data/pastures/test.toml");

  EXPECT_EQ(sward.grass.species_id, "ryegrass_perennial");
  EXPECT_DOUBLE_EQ(sward.grass.nitrogen_fixation_kg_per_t_dm, 0.0);
  EXPECT_EQ(sward.legume.species_id, "clover_white");
  EXPECT_DOUBLE_EQ(sward.legume.nitrogen_fixation_kg_per_t_dm, 25.0);
  EXPECT_TRUE(sward.validation_error().empty());

  const core::SwardParameters without_fixation = parse_sward(sward_text(""), "test.toml");
  EXPECT_DOUBLE_EQ(without_fixation.legume.nitrogen_fixation_kg_per_t_dm, 0.0);
}

TEST(PastureConfigTest, AnInvalidSpeciesIsReportedWithItsName) {
  std::string text = sward_text();
  const std::string::size_type position = text.find("optimum_temperature_c = 20.0");
  ASSERT_NE(position, std::string::npos);
  text.replace(position, std::string("optimum_temperature_c = 20.0").size(),
               "optimum_temperature_c = 2.0");

  try {
    static_cast<void>(parse_sward(text, "data/pastures/test.toml"));
    FAIL() << "expected the species to be rejected";
  } catch (const ConfigError& error) {
    EXPECT_NE(std::string(error.what()).find("cardinal temperatures"), std::string::npos);
    EXPECT_NE(std::string(error.what()).find("ryegrass_perennial"), std::string::npos);
  }
}

}  // namespace
}  // namespace paddock::config
