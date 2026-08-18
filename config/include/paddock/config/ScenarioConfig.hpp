// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <paddock/core/Farm.hpp>
#include <paddock/core/Farmer.hpp>
#include <paddock/core/FarmletGrid.hpp>
#include <paddock/core/GrazingCalendar.hpp>
#include <paddock/core/Pasture.hpp>
#include <paddock/core/Raster.hpp>
#include <paddock/core/Simulation.hpp>
#include <paddock/core/SoilWater.hpp>
#include <paddock/core/Weather.hpp>

namespace paddock::config {

/// One file a bundle depends on, and the hash it was built against.
struct BundleInput {
  std::string relative_path;
  std::string recorded_sha256;
  std::string actual_sha256;

  [[nodiscard]] bool matches() const noexcept { return recorded_sha256 == actual_sha256; }
};

/// An optional grid, turning a scenario from one hectare into a map.
///
/// The soil gradient is a demonstration: available water rising from the
/// western edge of the raster to the eastern one, which is enough to show a
/// shallow corner drying out first. Real soils arrive with S-map in M3, and
/// this section is what they will replace.
struct GridSpec {
  std::size_t cols = 0;
  std::size_t rows = 0;
  double cell_size_m = 0.0;
  double available_water_west_mm = 0.0;
  double available_water_east_mm = 0.0;

  /// Where the raster's north-west corner sits, NZTM2000 metres.
  double origin_easting = 0.0;
  double origin_northing = 0.0;

  /// Size to subdivide the grid into paddocks of, hectares. Zero leaves the
  /// farm undivided, which is a valid thing to model - a block grazed as one -
  /// but it is not a rotation, and a bundle carrying stock and a rotational
  /// calendar with nothing to rotate around is refused.
  double paddock_hectares = 0.0;
};

/// A mob as a scenario describes it: which class, how many, where they start
/// and in what condition.
///
/// The starting weight is worth its own field rather than being taken from the
/// species definition every time. Ewes coming out of a drought at 50 kg and
/// well-wintered ewes at 65 are the same class of animal on two different
/// farms, and which of them a run starts with changes the year that follows.
struct MobSpec {
  std::string name;
  std::size_t paddock = 0;
  int head = 0;

  /// Resolved from the species file the manifest points at.
  core::AnimalClassParameters animal;

  double liveweight_kg = 0.0;
  double age_days = 0.0;
};

/// A scenario bundle: everything needed to reproduce a run, in one directory.
///
/// ```
/// canterbury-baseline/
///   scenario.toml     the manifest below
///   weather.toml      a synthetic site, or a CliFlo snapshot
///   soil.toml
///   sward.toml
/// ```
///
/// ```toml
/// [scenario]
/// name = "canterbury_baseline"
/// engine_version = "0.1.0"
/// master_seed = 20240701
///
/// [run]
/// start_date = "2023-01-01"
/// end_date = "2023-12-31"
/// latitude_degrees = -43.5
///
/// [weather]
/// kind = "synthetic"          # or "snapshot"
/// path = "weather.toml"
/// sha256 = "..."
///
/// [soil]
/// path = "soil.toml"
/// sha256 = "..."
///
/// [sward]
/// path = "sward.toml"
/// sha256 = "..."
///
/// [initial_state]
/// soil_water_mm = 90.0
/// grass_kg_dm_per_ha = 1800.0
/// legume_kg_dm_per_ha = 400.0
/// soil_mineral_nitrogen_kg_per_ha = 60.0
/// ```
///
/// Every referenced file's SHA-256 is recorded in the manifest and checked on
/// load. A bundle whose inputs have changed underneath it is not the bundle
/// that produced the result, and saying so loudly is the difference between a
/// reproducible scenario and a directory of files.
struct ScenarioBundle {
  std::string name;
  std::string description;
  std::string engine_version;
  std::uint64_t master_seed = 0;

  core::DateRange range;
  double latitude_degrees = 0.0;
  core::FarmletInitialState initial_state;

  core::SoilWaterParameters soil;
  core::SwardParameters sward;

  /// Ready to run: a synthetic generator or a snapshot replay, already built.
  std::shared_ptr<core::WeatherSource> weather;

  /// Present when the manifest has a [grid] section.
  std::optional<GridSpec> grid;

  /// The stock the run starts with. Empty for a bundle that models pasture
  /// alone, which is what every bundle written before livestock existed does.
  std::vector<MobSpec> mobs;

  /// Which grazing system runs when. Empty when the bundle has no stock; a
  /// bundle with stock must say how they are managed, because leaving it to a
  /// default would make the most consequential decision in a pastoral model
  /// the one nobody wrote down.
  core::GrazingCalendar grazing;

  std::vector<BundleInput> inputs;

  /// True when every input still hashes to what the manifest recorded.
  [[nodiscard]] bool inputs_unchanged() const noexcept;

  /// The inputs that no longer match, for a message that names them.
  [[nodiscard]] std::vector<BundleInput> changed_inputs() const;

  [[nodiscard]] core::Farmlet make_farmlet() const;

  /// The soil raster the grid describes, with the bundle's soil parameters
  /// varied across it. Throws when the bundle has no [grid] section.
  [[nodiscard]] core::Raster<core::SoilWaterParameters> make_soil_raster() const;

  /// A grid of farmlets ready to step. Throws when there is no [grid] section.
  [[nodiscard]] core::FarmletGrid make_grid() const;

  /// The paddocks the grid subdivides into. Empty when `paddock_hectares` is
  /// zero.
  [[nodiscard]] std::vector<core::Paddock> make_paddocks() const;

  /// A farm ready to step: ground, paddocks and the stock standing on it.
  /// Throws when the bundle has no [grid] section or no paddocks.
  [[nodiscard]] core::Farm make_farm() const;

  /// The farmer who will move that stock. Throws when the bundle has no
  /// grazing calendar.
  [[nodiscard]] core::Farmer make_farmer() const;
};

/// Loads and validates a bundle directory.
///
/// Throws ConfigError - with file, line and column - for a malformed manifest,
/// a missing input, an input whose hash does not match, or an engine version
/// the running engine cannot reproduce.
[[nodiscard]] ScenarioBundle load_scenario(const std::string& bundle_directory);

/// Loads without enforcing hashes or the engine version. Used by the tool that
/// writes the hashes in the first place, and by nothing else.
[[nodiscard]] ScenarioBundle load_scenario_unchecked(const std::string& bundle_directory);

}  // namespace paddock::config
