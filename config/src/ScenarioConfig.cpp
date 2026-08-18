// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#include <algorithm>
#include <cstddef>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <paddock/config/PastureConfig.hpp>
#include <paddock/config/ScenarioConfig.hpp>
#include <paddock/config/SoilConfig.hpp>
#include <paddock/config/SpeciesConfig.hpp>
#include <paddock/config/WeatherConfig.hpp>
#include <paddock/core/Sha256.hpp>
#include <paddock/core/SnapshotWeather.hpp>
#include <paddock/core/SyntheticTerrain.hpp>
#include <paddock/core/SyntheticWeather.hpp>
#include <paddock/core/Version.hpp>

#include "TomlSupport.hpp"

namespace paddock::config {

namespace {

constexpr const char* kManifestName = "scenario.toml";

std::string join(const std::string& directory, const std::string& relative) {
  if (directory.empty()) {
    return relative;
  }
  const char last = directory.back();
  return last == '/' || last == '\\' ? directory + relative : directory + "/" + relative;
}

std::string read_file(const std::string& path, const toml::table& table,
                      const std::string& manifest_path) {
  const std::ifstream file(path, std::ios::binary);
  if (!file) {
    detail::throw_in(table, manifest_path, "cannot open bundle input '" + path + "'");
  }
  std::ostringstream contents;
  contents << file.rdbuf();
  return contents.str();
}

core::Date read_date(const toml::table& table, std::string_view key,
                     const std::string& manifest_path) {
  const std::string text = detail::require_string(table, key, manifest_path);
  if (text.size() != 10 || text[4] != '-' || text[7] != '-') {
    detail::throw_in(table, manifest_path,
                     "'" + std::string(key) + "' must be a date as YYYY-MM-DD, got '" + text + "'");
  }
  const core::Date date{std::stoi(text.substr(0, 4)), std::stoi(text.substr(5, 2)),
                        std::stoi(text.substr(8, 2))};
  if (!date.is_valid()) {
    detail::throw_in(table, manifest_path, "no such date: '" + text + "'");
  }
  return date;
}

/// Reads a `[section]` that names a file and the hash it was built against.
BundleInput read_input_table(const toml::table& table, const std::string& directory,
                             const std::string& manifest_path, std::string& contents) {
  BundleInput input;
  input.relative_path = detail::require_string(table, "path", manifest_path);
  input.recorded_sha256 = detail::optional_string(table, "sha256", "");
  contents = read_file(join(directory, input.relative_path), table, manifest_path);
  input.actual_sha256 = core::Sha256::hex_of(contents);
  return input;
}

BundleInput read_input(const toml::table& root, std::string_view section,
                       const std::string& directory, const std::string& manifest_path,
                       std::string& contents) {
  return read_input_table(detail::require_table(root, section, manifest_path), directory,
                          manifest_path, contents);
}

core::GrazingSystem grazing_system_of(const std::string& name, const toml::table& table,
                                      const std::string& path) {
  if (name == "set_stocking") {
    return core::GrazingSystem::SetStocking;
  }
  if (name == "rotational") {
    return core::GrazingSystem::Rotational;
  }
  detail::throw_in(table, path,
                   "unknown grazing system '" + name +
                       "'. Known systems are: set_stocking, rotational. The shuffle is not one of "
                       "them: it is what rotation becomes on a farm with too few paddocks, so it "
                       "emerges rather than being chosen");
}

core::Date date_of(const toml::table& table, std::string_view key, const std::string& path) {
  const std::string text = detail::require_string(table, key, path);
  if (text.size() != 10 || text[4] != '-' || text[7] != '-') {
    detail::throw_in(
        table, path,
        "'" + std::string(key) + "' must be an ISO date like 2023-07-01, found '" + text + "'");
  }
  core::Date date;
  date.year = std::stoi(text.substr(0, 4));
  date.month = std::stoi(text.substr(5, 2));
  date.day = std::stoi(text.substr(8, 2));
  if (!date.is_valid()) {
    detail::throw_in(table, path, "'" + std::string(key) + "' is not a real date: " + text);
  }
  return date;
}

ScenarioBundle read(const std::string& directory, bool enforce) {
  const std::string manifest_path = join(directory, kManifestName);
  const toml::table root = detail::parse_file(manifest_path);
  detail::reject_unknown_keys(root,
                              {"scenario", "run", "weather", "soil", "sward", "initial_state",
                               "grid", "mob", "grazing_period"},
                              manifest_path, "the manifest");

  const toml::table& scenario = detail::require_table(root, "scenario", manifest_path);
  detail::reject_unknown_keys(scenario, {"name", "description", "engine_version", "master_seed"},
                              manifest_path, "[scenario]");

  ScenarioBundle bundle;
  bundle.name = detail::require_string(scenario, "name", manifest_path);
  bundle.description = detail::optional_string(scenario, "description", "");
  bundle.engine_version = detail::require_string(scenario, "engine_version", manifest_path);
  bundle.master_seed =
      static_cast<std::uint64_t>(detail::require_double(scenario, "master_seed", manifest_path));

  if (enforce && bundle.engine_version != core::engine_version()) {
    // A bundle reproduces bit-for-bit on the engine version that produced it,
    // and this engine cannot promise anything about another one's output.
    detail::throw_in(scenario, manifest_path,
                     "bundle was produced by engine " + bundle.engine_version + ", this is " +
                         core::engine_version() +
                         ". Re-record the bundle, or run the engine version it names.");
  }

  const toml::table& run = detail::require_table(root, "run", manifest_path);
  detail::reject_unknown_keys(run, {"start_date", "end_date", "latitude_degrees"}, manifest_path,
                              "[run]");
  bundle.range.first = read_date(run, "start_date", manifest_path);
  bundle.range.last = read_date(run, "end_date", manifest_path);
  if (!bundle.range.is_valid()) {
    detail::throw_in(run, manifest_path, "'end_date' must not be before 'start_date'");
  }
  bundle.latitude_degrees = detail::require_double(run, "latitude_degrees", manifest_path);

  const toml::table& initial = detail::require_table(root, "initial_state", manifest_path);
  detail::reject_unknown_keys(initial,
                              {"soil_water_mm", "grass_kg_dm_per_ha", "legume_kg_dm_per_ha",
                               "soil_mineral_nitrogen_kg_per_ha"},
                              manifest_path, "[initial_state]");
  bundle.initial_state.soil_water_mm =
      detail::require_double(initial, "soil_water_mm", manifest_path);
  bundle.initial_state.grass_kg_dm_per_ha =
      detail::require_double(initial, "grass_kg_dm_per_ha", manifest_path);
  bundle.initial_state.legume_kg_dm_per_ha =
      detail::require_double(initial, "legume_kg_dm_per_ha", manifest_path);
  bundle.initial_state.soil_mineral_nitrogen_kg_per_ha =
      detail::require_double(initial, "soil_mineral_nitrogen_kg_per_ha", manifest_path);

  std::string soil_text;
  const BundleInput soil_input = read_input(root, "soil", directory, manifest_path, soil_text);
  bundle.soil = parse_soil(soil_text, join(directory, soil_input.relative_path)).water;

  std::string sward_text;
  const BundleInput sward_input = read_input(root, "sward", directory, manifest_path, sward_text);
  bundle.sward = parse_sward(sward_text, join(directory, sward_input.relative_path));

  const toml::table& weather = detail::require_table(root, "weather", manifest_path);
  detail::reject_unknown_keys(weather, {"kind", "path", "sha256", "dataset", "licence"},
                              manifest_path, "[weather]");
  const std::string kind = detail::require_string(weather, "kind", manifest_path);
  std::string weather_text;
  const BundleInput weather_input =
      read_input(root, "weather", directory, manifest_path, weather_text);

  if (kind == "synthetic") {
    bundle.weather = std::make_shared<core::SyntheticWeatherSource>(
        parse_synthetic_weather(weather_text, join(directory, weather_input.relative_path)),
        bundle.master_seed);
  } else if (kind == "snapshot") {
    core::SnapshotWeatherSource::Options options;
    options.path = join(directory, weather_input.relative_path);
    options.dataset = detail::optional_string(weather, "dataset", weather_input.relative_path);
    options.licence = detail::optional_string(weather, "licence", "");
    options.expected_content_hash = weather_input.recorded_sha256;
    auto source = std::make_shared<core::SnapshotWeatherSource>(options);
    const core::ConnectionStatus status = source->test_connection();
    if (!status.ok) {
      detail::throw_in(weather, manifest_path, status.message);
    }
    bundle.weather = std::move(source);
  } else {
    detail::throw_in(weather, manifest_path,
                     "unknown weather kind '" + kind + "'. Known kinds are: synthetic, snapshot");
  }

  if (detail::has(root, "grid")) {
    const toml::table& grid = detail::require_table(root, "grid", manifest_path);
    detail::reject_unknown_keys(
        grid,
        {"cols", "rows", "cell_size_m", "available_water_west_mm", "available_water_east_mm",
         "origin_easting", "origin_northing", "paddock_hectares"},
        manifest_path, "[grid]");
    GridSpec spec;
    spec.cols = static_cast<std::size_t>(detail::require_double(grid, "cols", manifest_path));
    spec.rows = static_cast<std::size_t>(detail::require_double(grid, "rows", manifest_path));
    spec.cell_size_m = detail::require_double(grid, "cell_size_m", manifest_path);
    spec.available_water_west_mm =
        detail::require_double(grid, "available_water_west_mm", manifest_path);
    spec.available_water_east_mm =
        detail::require_double(grid, "available_water_east_mm", manifest_path);
    spec.origin_easting = detail::optional_double(grid, "origin_easting", 1570000.0, manifest_path);
    spec.origin_northing =
        detail::optional_double(grid, "origin_northing", 5180000.0, manifest_path);

    if (spec.cols == 0 || spec.rows == 0) {
      detail::throw_in(grid, manifest_path, "'cols' and 'rows' must both be at least one");
    }
    if (spec.cell_size_m <= 0.0) {
      detail::throw_in(grid, manifest_path, "'cell_size_m' must be positive");
    }
    if (spec.available_water_west_mm <= 0.0 || spec.available_water_east_mm <= 0.0) {
      detail::throw_in(grid, manifest_path, "available water must be positive at both edges");
    }
    spec.paddock_hectares = detail::optional_double(grid, "paddock_hectares", 0.0, manifest_path);
    bundle.grid = spec;
  }

  // The ground. Absent means flat, which is what every bundle written before
  // this section existed was, and keeps their results exactly as they were.
  if (const toml::table* terrain = root["terrain"].as_table(); terrain != nullptr) {
    const std::string terrain_kind = detail::require_string(*terrain, "kind", manifest_path);
    if (terrain_kind == "flat") {
      detail::reject_unknown_keys(*terrain, {"kind"}, manifest_path,
                                  "[terrain] with kind = \"flat\"");
      bundle.terrain.kind = TerrainSpec::Kind::Flat;
    } else if (terrain_kind == "synthetic") {
      detail::reject_unknown_keys(*terrain,
                                  {"kind", "base_elevation_m", "gradient_east", "gradient_north",
                                   "undulation_amplitude_m", "undulation_wavelength_m"},
                                  manifest_path, "[terrain] with kind = \"synthetic\"");
      bundle.terrain.kind = TerrainSpec::Kind::Synthetic;
      core::SyntheticSurface& surface = bundle.terrain.surface;
      surface.base_elevation_m = detail::optional_double(*terrain, "base_elevation_m",
                                                         surface.base_elevation_m, manifest_path);
      surface.gradient_east =
          detail::optional_double(*terrain, "gradient_east", surface.gradient_east, manifest_path);
      surface.gradient_north = detail::optional_double(*terrain, "gradient_north",
                                                       surface.gradient_north, manifest_path);
      surface.undulation_amplitude_m = detail::optional_double(
          *terrain, "undulation_amplitude_m", surface.undulation_amplitude_m, manifest_path);
      surface.undulation_wavelength_m = detail::optional_double(
          *terrain, "undulation_wavelength_m", surface.undulation_wavelength_m, manifest_path);
      if (surface.undulation_wavelength_m <= 0.0) {
        detail::throw_in(*terrain, manifest_path,
                         "'undulation_wavelength_m' must be positive; a wavelength of zero is not "
                         "a flat surface, it is a division by zero");
      }
    } else {
      detail::throw_in(
          *terrain, manifest_path,
          "unknown terrain kind '" + terrain_kind + "'. Known kinds are: flat, synthetic");
    }
  }

  // The stock, if any. A species is referenced the way every other input is:
  // by relative path with its hash recorded, so a bundle stays reproducible and
  // can still share data/species/ rather than copying it.
  std::vector<BundleInput> mob_inputs;
  if (const toml::node* mobs_node = root.get("mob"); mobs_node != nullptr) {
    const toml::array* mobs = mobs_node->as_array();
    if (mobs == nullptr) {
      detail::throw_at(*mobs_node, manifest_path, "[[mob]] must be a list of mobs");
    }
    for (const toml::node& element : *mobs) {
      const toml::table* entry = element.as_table();
      if (entry == nullptr) {
        detail::throw_at(element, manifest_path, "each [[mob]] must be a table");
      }
      detail::reject_unknown_keys(
          *entry, {"name", "path", "sha256", "head", "paddock", "liveweight_kg", "age_days"},
          manifest_path, "[[mob]]");

      MobSpec mob;
      mob.name = detail::require_string(*entry, "name", manifest_path);
      mob.head = static_cast<int>(detail::require_double(*entry, "head", manifest_path));
      mob.paddock =
          static_cast<std::size_t>(detail::require_double(*entry, "paddock", manifest_path));

      std::string species_text;
      const BundleInput species_input =
          read_input_table(*entry, directory, manifest_path, species_text);
      const SpeciesDefinition species =
          parse_species(species_text, join(directory, species_input.relative_path));
      mob.animal = species.energy;

      // The species supplies a typical animal; the scenario may start with a
      // different one, and usually does.
      mob.liveweight_kg = detail::optional_double(*entry, "liveweight_kg",
                                                  species.typical_liveweight_kg, manifest_path);
      mob.age_days =
          detail::optional_double(*entry, "age_days", species.typical_age_days, manifest_path);

      if (mob.head <= 0) {
        detail::throw_in(*entry, manifest_path, "mob '" + mob.name + "' needs at least one animal");
      }
      if (mob.liveweight_kg <= 0.0) {
        detail::throw_in(*entry, manifest_path, "mob '" + mob.name + "' needs a positive weight");
      }

      mob_inputs.push_back(species_input);
      bundle.mobs.push_back(std::move(mob));
    }
  }

  // How they are managed. Required once there is stock: leaving it to a default
  // would make the most consequential decision in a pastoral model the one
  // nobody wrote down.
  std::vector<core::GrazingPeriod> periods;
  if (const toml::node* periods_node = root.get("grazing_period"); periods_node != nullptr) {
    const toml::array* entries = periods_node->as_array();
    if (entries == nullptr) {
      detail::throw_at(*periods_node, manifest_path, "[[grazing_period]] must be a list");
    }
    for (const toml::node& element : *entries) {
      const toml::table* entry = element.as_table();
      if (entry == nullptr) {
        detail::throw_at(element, manifest_path, "each [[grazing_period]] must be a table");
      }
      detail::reject_unknown_keys(
          *entry, {"name", "from", "to", "system", "maximum_graze_days", "minimum_spell_days"},
          manifest_path, "[[grazing_period]]");

      core::GrazingPeriod period;
      period.name = detail::require_string(*entry, "name", manifest_path);
      period.dates.first = date_of(*entry, "from", manifest_path);
      period.dates.last = date_of(*entry, "to", manifest_path);
      period.rule.system = grazing_system_of(
          detail::require_string(*entry, "system", manifest_path), *entry, manifest_path);
      period.rule.maximum_graze_days = static_cast<int>(
          detail::optional_double(*entry, "maximum_graze_days", 0.0, manifest_path));
      period.rule.minimum_spell_days = static_cast<int>(
          detail::optional_double(*entry, "minimum_spell_days", 0.0, manifest_path));
      periods.push_back(std::move(period));
    }
  }

  if (!bundle.mobs.empty() && periods.empty()) {
    detail::throw_in(root, manifest_path,
                     "this bundle carries stock but no [[grazing_period]]. How the stock are "
                     "managed is the most consequential decision in a pastoral model and it has "
                     "to be written down rather than defaulted");
  }
  if (!periods.empty()) {
    bundle.grazing = core::GrazingCalendar(std::move(periods));
    const std::string calendar_error = bundle.grazing.validation_error(bundle.range);
    if (!calendar_error.empty()) {
      detail::throw_in(root, manifest_path,
                       "the grazing calendar does not cover the run: " + calendar_error);
    }
  }

  bundle.inputs = {weather_input, soil_input, sward_input};
  bundle.inputs.insert(bundle.inputs.end(), mob_inputs.begin(), mob_inputs.end());

  if (enforce) {
    const std::vector<BundleInput> changed = bundle.changed_inputs();
    if (!changed.empty()) {
      std::string detail_message =
          "bundle inputs have changed since the hashes were recorded, so this is not the bundle "
          "that produced the result:";
      for (const BundleInput& input : changed) {
        detail_message += "\n  " + input.relative_path + "\n    recorded " +
                          (input.recorded_sha256.empty() ? "(nothing)" : input.recorded_sha256) +
                          "\n    actual   " + input.actual_sha256;
      }
      detail::throw_in(root, manifest_path, detail_message);
    }
  }

  return bundle;
}

}  // namespace

bool ScenarioBundle::inputs_unchanged() const noexcept {
  return std::all_of(inputs.begin(), inputs.end(),
                     [](const BundleInput& input) { return input.matches(); });
}

std::vector<BundleInput> ScenarioBundle::changed_inputs() const {
  std::vector<BundleInput> changed;
  for (const BundleInput& input : inputs) {
    if (!input.matches()) {
      changed.push_back(input);
    }
  }
  return changed;
}

core::Farmlet ScenarioBundle::make_farmlet() const {
  return {soil, sward, initial_state, latitude_degrees};
}

core::Raster<core::SoilWaterParameters> ScenarioBundle::make_soil_raster() const {
  if (!grid.has_value()) {
    throw std::runtime_error("scenario '" + name + "' has no [grid] section, so it has no map");
  }
  const GridSpec& spec = *grid;

  core::GeoTransform transform;
  transform.origin_easting = spec.origin_easting;
  transform.origin_northing = spec.origin_northing;
  transform.cell_size = spec.cell_size_m;

  core::Raster<core::SoilWaterParameters> soils(spec.cols, spec.rows, transform, soil);
  const double span = spec.cols > 1 ? static_cast<double>(spec.cols - 1) : 1.0;
  for (std::size_t row = 0; row < spec.rows; ++row) {
    for (std::size_t col = 0; col < spec.cols; ++col) {
      core::SoilWaterParameters cell = soil;
      const double weight = static_cast<double>(col) / span;
      cell.total_available_water_mm =
          spec.available_water_west_mm +
          (weight * (spec.available_water_east_mm - spec.available_water_west_mm));
      soils(col, row) = cell;
    }
  }
  return soils;
}

std::optional<core::Raster<double>> ScenarioBundle::make_elevation() const {
  if (terrain.is_flat()) {
    return std::nullopt;
  }
  if (!grid.has_value()) {
    throw std::runtime_error("scenario '" + name +
                             "' describes terrain but has no [grid] section to sample it over");
  }
  const GridSpec& spec = *grid;
  const double width_m = static_cast<double>(spec.cols) * spec.cell_size_m;
  const double height_m = static_cast<double>(spec.rows) * spec.cell_size_m;

  core::BoundingBox area = core::BoundingBox::empty();
  area.expand_to_include(core::Point2D{spec.origin_easting, spec.origin_northing - height_m});
  area.expand_to_include(core::Point2D{spec.origin_easting + width_m, spec.origin_northing});

  return core::SyntheticElevationSource(terrain.surface).fetch(area, spec.cell_size_m);
}

std::optional<core::Topography> ScenarioBundle::make_topography() const {
  const std::optional<core::Raster<double>> elevation = make_elevation();
  if (!elevation.has_value()) {
    return std::nullopt;
  }
  return core::topography_of(*elevation);
}

core::FarmletGrid ScenarioBundle::make_grid() const {
  core::FarmletGrid built(make_soil_raster(), sward, initial_state, latitude_degrees);
  // Radiation by slope and aspect, which is what makes a south face grow less
  // than the north face of the same hill. Left alone on flat ground, where the
  // ratio is one everywhere and computing it would only cost time.
  if (const std::optional<core::Topography> ground = make_topography(); ground.has_value()) {
    built.set_terrain(*ground);
  }
  return built;
}

std::vector<core::Paddock> ScenarioBundle::make_paddocks() const {
  if (!grid.has_value() || grid->paddock_hectares <= 0.0) {
    return {};
  }
  const GridSpec& spec = *grid;

  // The grid already says where the farm is and how big it is, so paddocks
  // subdivide that rather than declaring an extent of their own. Two sources of
  // truth for where a farm sits is one too many.
  const double width_m = static_cast<double>(spec.cols) * spec.cell_size_m;
  const double height_m = static_cast<double>(spec.rows) * spec.cell_size_m;

  core::BoundingBox area = core::BoundingBox::empty();
  area.expand_to_include(core::Point2D{spec.origin_easting, spec.origin_northing - height_m});
  area.expand_to_include(core::Point2D{spec.origin_easting + width_m, spec.origin_northing});

  return core::SyntheticParcelSource(spec.paddock_hectares).fetch(area);
}

core::Farm ScenarioBundle::make_farm() const {
  if (!grid.has_value()) {
    throw std::runtime_error("scenario '" + name + "' has no [grid] section, so it has no ground");
  }
  std::vector<core::Paddock> paddocks = make_paddocks();
  if (paddocks.empty()) {
    throw std::runtime_error("scenario '" + name +
                             "' has no paddocks: set 'paddock_hectares' in [grid]");
  }

  const GridSpec& spec = *grid;
  core::GeoTransform transform;
  transform.origin_easting = spec.origin_easting;
  transform.origin_northing = spec.origin_northing;
  transform.cell_size = spec.cell_size_m;

  // The mask needs the grid's shape and georeferencing, not its values.
  const core::Raster<double> shape(spec.cols, spec.rows, transform, 0.0);
  core::PaddockMask mask(shape, paddocks);

  core::Farm farm(make_grid(), std::move(mask), std::move(paddocks));

  // What it costs a mob to walk the paddock it is grazing, TMC Eq. 23. Without
  // this the farm is a terrace whatever its terrain section says, and the two
  // halves of the terrain model would disagree: the grass would know it was on
  // a hill and the animals would not.
  if (const std::optional<core::Topography> ground = make_topography(); ground.has_value()) {
    farm.set_slopes(ground->slope_degrees);
  }

  for (const MobSpec& spec_mob : mobs) {
    core::Mob mob;
    mob.name = spec_mob.name;
    mob.head = spec_mob.head;
    mob.animal = spec_mob.animal;
    mob.state.liveweight_kg = spec_mob.liveweight_kg;
    mob.state.age_days = spec_mob.age_days;
    mob.state.liveweight_change_kg_per_day = 0.0;
    farm.add_mob(std::move(mob), spec_mob.paddock);
  }
  return farm;
}

core::Farmer ScenarioBundle::make_farmer() const {
  if (grazing.empty()) {
    throw std::runtime_error("scenario '" + name + "' has no grazing calendar");
  }
  return core::Farmer(grazing);
}

ScenarioBundle load_scenario(const std::string& bundle_directory) {
  return read(bundle_directory, /*enforce=*/true);
}

ScenarioBundle load_scenario_unchecked(const std::string& bundle_directory) {
  return read(bundle_directory, /*enforce=*/false);
}

}  // namespace paddock::config
