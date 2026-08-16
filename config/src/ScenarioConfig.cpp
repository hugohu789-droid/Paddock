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
#include <paddock/config/WeatherConfig.hpp>
#include <paddock/core/Sha256.hpp>
#include <paddock/core/SnapshotWeather.hpp>
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
BundleInput read_input(const toml::table& root, std::string_view section,
                       const std::string& directory, const std::string& manifest_path,
                       std::string& contents) {
  const toml::table& table = detail::require_table(root, section, manifest_path);
  BundleInput input;
  input.relative_path = detail::require_string(table, "path", manifest_path);
  input.recorded_sha256 = detail::optional_string(table, "sha256", "");
  contents = read_file(join(directory, input.relative_path), table, manifest_path);
  input.actual_sha256 = core::Sha256::hex_of(contents);
  return input;
}

ScenarioBundle read(const std::string& directory, bool enforce) {
  const std::string manifest_path = join(directory, kManifestName);
  const toml::table root = detail::parse_file(manifest_path);
  detail::reject_unknown_keys(
      root, {"scenario", "run", "weather", "soil", "sward", "initial_state", "grid"}, manifest_path,
      "the manifest");

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
    detail::reject_unknown_keys(grid,
                                {"cols", "rows", "cell_size_m", "available_water_west_mm",
                                 "available_water_east_mm", "origin_easting", "origin_northing"},
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
    bundle.grid = spec;
  }

  bundle.inputs = {weather_input, soil_input, sward_input};

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

core::FarmletGrid ScenarioBundle::make_grid() const {
  return {make_soil_raster(), sward, initial_state, latitude_degrees};
}

ScenarioBundle load_scenario(const std::string& bundle_directory) {
  return read(bundle_directory, /*enforce=*/true);
}

ScenarioBundle load_scenario_unchecked(const std::string& bundle_directory) {
  return read(bundle_directory, /*enforce=*/false);
}

}  // namespace paddock::config
