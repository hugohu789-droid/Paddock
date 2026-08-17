// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#include <exception>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <paddock/core/SnapshotWeather.hpp>
#include <paddock/core/Version.hpp>
#include <paddock/core/Weather.hpp>

#ifdef PADDOCK_WITH_CONFIG
#include <paddock/config/ScenarioConfig.hpp>
#include <paddock/core/Simulation.hpp>
#endif

namespace {

void print_usage(std::ostream& out) {
  out << "paddock " << paddock::core::engine_version() << "\n\n"
      << "Usage:\n"
      << "  paddock --version                    Print the engine version\n"
      << "  paddock --help                       Print this message\n"
      << "  paddock source test <snapshot.csv>   Check a weather snapshot\n"
#ifdef PADDOCK_WITH_CONFIG
      << "  paddock scenario run <bundle> [--csv <file>]\n"
      << "                                       Run a scenario bundle\n"
#endif
      << "\nWeather snapshots are produced by scripts/cliflo-snapshot.py.\n";
}

/// `paddock source test <path>` - the CLI face of the DataSource port. It has
/// to answer the only question that matters before a run: can this source
/// deliver data, and if not, what should I do about it?
int test_source(const std::string& path) {
  paddock::core::SnapshotWeatherSource::Options options;
  options.path = path;
  options.dataset = path;
  const paddock::core::SnapshotWeatherSource source(options);

  const paddock::core::SourceDescription description = source.describe();
  const paddock::core::ConnectionStatus status = source.test_connection();

  std::ostream& out = status.ok ? std::cout : std::cerr;
  out << description.name << '\n'
      << "  licence   " << description.licence << '\n'
      << "  coverage  " << description.coverage << '\n'
      << "  cadence   " << description.cadence << '\n'
      << "  status    " << (status.ok ? "ok" : "unavailable") << '\n'
      << "            " << status.message << '\n';
  return status.ok ? 0 : 1;
}

#ifdef PADDOCK_WITH_CONFIG

void write_csv(const paddock::core::RunResult& result, const std::string& path) {
  std::ofstream csv(path, std::ios::binary);
  if (!csv) {
    throw std::runtime_error("cannot write '" + path + "'");
  }
  csv << "date,rainfall_mm,evapotranspiration_mm,drainage_mm,soil_water_mm,"
         "water_stress,growth_kg_dm_per_ha,cover_kg_dm_per_ha,legume_fraction,"
         "soil_mineral_nitrogen_kg_per_ha\n";
  csv << std::setprecision(10);
  for (const paddock::core::DailyRecord& day : result.daily) {
    csv << day.date.to_iso_string() << ',' << day.rainfall_mm << ',' << day.evapotranspiration_mm
        << ',' << day.drainage_mm << ',' << day.soil_water_mm << ',' << day.water_stress_coefficient
        << ',' << day.growth_kg_dm << ',' << day.cover_kg_dm << ',' << day.legume_fraction << ','
        << day.soil_mineral_nitrogen_kg << '\n';
  }
  std::cout << "wrote " << result.daily.size() << " daily records to " << path << '\n';
}

/// `paddock scenario run <bundle>` - loads a bundle, checks its inputs are the
/// ones it was built on, runs it, and reports. A run whose budgets do not close
/// is reported as a failure: the numbers would be meaningless.
int run_scenario(const std::string& bundle_directory, const std::string& csv_path) {
  const paddock::config::ScenarioBundle bundle = paddock::config::load_scenario(bundle_directory);
  paddock::core::Farmlet farmlet = bundle.make_farmlet();
  const paddock::core::RunResult result =
      paddock::core::run(farmlet, *bundle.weather, bundle.range);
  const paddock::core::RunSummary& summary = result.summary;

  std::cout << bundle.name << " (engine " << bundle.engine_version << ", seed "
            << bundle.master_seed << ")\n"
            << "  " << bundle.range.first.to_iso_string() << " to "
            << bundle.range.last.to_iso_string() << ", " << summary.days << " days\n"
            << "  weather   " << result.weather_provenance.source_name << ':'
            << result.weather_provenance.dataset << " ("
            << result.weather_provenance.content_hash.substr(0, 12) << ")\n";
  std::cout << std::fixed << std::setprecision(1) << "  rainfall  " << summary.total_rainfall_mm
            << " mm\n"
            << "  et        " << summary.total_evapotranspiration_mm << " mm\n"
            << "  drainage  " << summary.total_drainage_mm << " mm\n"
            << "  growth    " << summary.total_growth_kg_dm << " kg DM/ha\n"
            << "  fixed N   " << summary.total_nitrogen_fixed_kg << " kg N/ha\n"
            << "  closing   " << summary.closing_cover_kg_dm << " kg DM/ha cover, "
            << summary.closing_soil_water_mm << " mm soil water, " << std::setprecision(0)
            << summary.closing_legume_fraction * 100.0 << "% legume\n";

  if (!result.budgets_close(farmlet)) {
    std::cerr << "\npaddock: the budgets did not close; these results are not usable\n"
              << result.ledger.report(paddock::core::Budget::Water, farmlet.soil().water_mm())
              << result.ledger.report(paddock::core::Budget::DryMatter,
                                      farmlet.sward().cover_kg_dm())
              << result.ledger.report(paddock::core::Budget::Nitrogen,
                                      farmlet.sward().total_nitrogen_kg());
    return 1;
  }

  if (!csv_path.empty()) {
    write_csv(result, csv_path);
  }
  return 0;
}

#endif  // PADDOCK_WITH_CONFIG

}  // namespace

int main(int argc, char** argv) {
  try {
    const std::vector<std::string_view> args(argv + 1, argv + argc);

    if (args.empty() || args.front() == "--help" || args.front() == "-h") {
      print_usage(std::cout);
      return 0;
    }

    if (args.front() == "--version") {
      std::cout << paddock::core::engine_version() << '\n';
      return 0;
    }

    if (args.size() == 3 && args[0] == "source" && args[1] == "test") {
      return test_source(std::string(args[2]));
    }

#ifdef PADDOCK_WITH_CONFIG
    if (args.size() >= 3 && args[0] == "scenario" && args[1] == "run") {
      std::string csv_path;
      if (args.size() == 5 && args[3] == "--csv") {
        csv_path = std::string(args[4]);
      } else if (args.size() != 3) {
        std::cerr << "paddock: expected 'scenario run <bundle> [--csv <file>]'\n";
        return 2;
      }
      return run_scenario(std::string(args[2]), csv_path);
    }
#endif

    std::cerr << "paddock: unrecognised command\n";
    print_usage(std::cerr);
    return 2;
  } catch (const std::exception& error) {
    std::cerr << "paddock: " << error.what() << '\n';
    return 1;
  }
}
