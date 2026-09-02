// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <optional>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <paddock/config/DiseaseConfig.hpp>
#include <paddock/config/DiseaseReport.hpp>
#include <paddock/core/Sha256.hpp>
#include <paddock/core/SnapshotWeather.hpp>

#ifdef PADDOCK_WITH_GIS
#include <paddock/gis/ElevationDownload.hpp>
#include <paddock/gis/GeoPackageParcels.hpp>
#include <paddock/gis/GeoTiffElevation.hpp>
#endif
#include <paddock/core/Version.hpp>
#include <paddock/core/Weather.hpp>

#ifdef PADDOCK_WITH_CONFIG
#include <paddock/config/EconomicsConfig.hpp>
#include <paddock/config/FarmDashboard.hpp>
#include <paddock/config/NitrogenReport.hpp>
#include <paddock/config/ScenarioConfig.hpp>
#include <paddock/core/Simulation.hpp>

#include "AttachElevation.hpp"
#endif

namespace {

void print_usage(std::ostream& out) {
  out << "paddock " << paddock::core::engine_version() << "\n\n"
      << "Usage:\n"
      << "  paddock --version                    Print the engine version\n"
      << "  paddock --help                       Print this message\n"
      << "  paddock source test <file>           Check a data source:\n"
      << "                                       .csv weather, .tif elevation,\n"
      << "                                       .gpkg paddock boundaries\n"
#ifdef PADDOCK_WITH_CONFIG
      << "  paddock scenario run <bundle> [--csv <file>]\n"
      << "                                       Run a scenario bundle\n"
#endif
      << "\nWeather snapshots are produced by scripts/cliflo-snapshot.py.\n";
}

/// Lower-cased extension of `path`, including the dot, or an empty string.
std::string extension_of(const std::string& path) {
  const std::size_t dot = path.find_last_of('.');
  if (dot == std::string::npos) {
    return {};
  }
  std::string extension = path.substr(dot);
  for (char& character : extension) {
    character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
  }
  return extension;
}

int report(const paddock::core::SourceDescription& description,
           const paddock::core::ConnectionStatus& status) {
  std::ostream& out = status.ok ? std::cout : std::cerr;
  out << description.name << '\n'
      << "  licence   " << description.licence << '\n'
      << "  coverage  " << description.coverage << '\n'
      << "  cadence   " << description.cadence << '\n'
      << "  status    " << (status.ok ? "ok" : "unavailable") << '\n'
      << "            " << status.message << '\n';
  return status.ok ? 0 : 1;
}

/// `paddock source test <path>` - the CLI face of the DataSource port. It has
/// to answer the only question that matters before a run: can this source
/// deliver data, and if not, what should I do about it?
///
/// Which kind of source is decided by the file extension. That is a small lie
/// of convenience - a GeoTIFF is a GeoTIFF whatever it is called - but the
/// alternative is a --type flag everyone has to remember, and a misnamed file
/// still fails with GDAL's own message about the format it actually found.
int test_source(const std::string& path) {
  const std::string extension = extension_of(path);

#ifdef PADDOCK_WITH_GIS
  if (extension == ".tif" || extension == ".tiff") {
    const paddock::gis::GeoTiffElevationSource elevation(path);
    return report(elevation.describe(), elevation.test_connection());
  }
  if (extension == ".gpkg") {
    const paddock::gis::GeoPackageParcelSource parcels(path);
    return report(parcels.describe(), parcels.test_connection());
  }
#else
  if (extension == ".tif" || extension == ".tiff" || extension == ".gpkg") {
    std::cerr << "This build has no geospatial support, so " << path << " cannot be checked.\n"
              << "Configure with -DPADDOCK_BUILD_GIS=ON; it needs GDAL and PROJ, and\n"
              << "docs/setup.md says how to get them.\n";
    return 1;
  }
#endif

  paddock::core::SnapshotWeatherSource::Options options;
  options.path = path;
  options.dataset = path;
  const paddock::core::SnapshotWeatherSource weather(options);
  return report(weather.describe(), weather.test_connection());
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

/// `paddock disease <disease.toml> <weather.csv>...`
///
/// **The question this answers is not "what is the spore count", it is "how
/// many years out of ten would this farm have run a zinc programme".** One
/// weather file gives the year-by-year table; several give the comparison
/// between places, which is the form that says whether the disease is a
/// question for a farm at all.
///
/// The site's name is the weather file's parent directory, which is the bundle
/// it belongs to - so `data/scenarios/ruakura-fe/weather-2015-2025.csv` reports
/// as "ruakura-fe" without anyone having to name it twice.
int run_disease(const std::vector<std::string>& arguments) {
  const paddock::config::DiseaseDefinition disease =
      paddock::config::load_disease(arguments.front());

  std::vector<paddock::config::DiseaseSite> sites;
  for (std::size_t i = 1; i < arguments.size(); ++i) {
    const std::filesystem::path weather_path(arguments[i]);

    paddock::core::SnapshotWeatherSource::Options options;
    options.path = arguments[i];
    options.dataset = "recorded weather";
    options.licence = "see the snapshot's provenance file";

    const paddock::core::SnapshotWeatherSource source(options);
    const paddock::core::ConnectionStatus status = source.test_connection();
    if (!status.ok) {
      std::cerr << "paddock: " << status.message << "\n";
      return 1;
    }

    paddock::config::DiseaseSite site;
    site.name = weather_path.parent_path().filename().string();
    if (site.name.empty()) {
      site.name = weather_path.stem().string();
    }
    site.weather.records = source.records();
    sites.push_back(std::move(site));
  }

  std::cout << (sites.size() == 1 ? paddock::config::render_disease_years(sites.front(), disease)
                                  : paddock::config::render_disease_comparison(sites, disease));
  return 0;
}

/// `paddock scenario run <bundle>` - loads a bundle, checks its inputs are the
/// ones it was built on, runs it, and reports. A run whose budgets do not close
/// is reported as a failure: the numbers would be meaningless.
/// `paddock nitrogen <bundle> <regulation.toml> [<year> ...]`
///
/// **A compliance figure is a quotation, so the rule is an argument.** New
/// Zealand has no national nitrogen loss limit - regional councils set them
/// catchment by catchment - so a command that carried one built in would be
/// inventing a regulation. The file names the zone, the authority and the plan,
/// and the report quotes all three.
///
/// **Several years, because one says almost nothing.** Leaching moves with
/// drainage and drainage is weather: a farm can leach three times as much in a
/// wet year without having changed anything it does. Given more than one year
/// this prints them side by side, with leaching per millimetre of drainage
/// beside the total, which is the column that separates the two.
/// `paddock dashboard <bundle> [<year> ...] [--csv <stem>]`
///
/// **Every indicator carries how much it can be trusted, and the export carries
/// it too.** A dashboard is the easiest place in a project like this to undo its
/// own discipline: a tile reading "27.3 kg N/ha" is read as a measurement, and
/// it is a model output resting on a placeholder. So the trust column is in the
/// page, in the CSV, and counted in a panel of its own.
int run_dashboard(const std::vector<std::string>& arguments) {
  std::string csv_stem;
  std::string rule_path;
  std::string economics_path;
  std::vector<int> years;
  for (std::size_t i = 1; i < arguments.size(); ++i) {
    if (arguments[i] == "--csv" && i + 1 < arguments.size()) {
      csv_stem = arguments[i + 1];
      ++i;
      continue;
    }
    if (arguments[i] == "--rule" && i + 1 < arguments.size()) {
      rule_path = arguments[i + 1];
      ++i;
      continue;
    }
    if (arguments[i] == "--economics" && i + 1 < arguments.size()) {
      economics_path = arguments[i + 1];
      ++i;
      continue;
    }
    years.push_back(std::stoi(arguments[i]));
  }

  paddock::config::ScenarioBundle bundle = paddock::config::load_scenario(arguments.front());
  // **Missing ground is a warning, not a refusal**, and this said otherwise for
  // as long as the command existed.
  //
  // attach_elevation demotes a scenario whose snapshot is absent to flat and
  // hands back a sentence to show somebody - that is its documented contract,
  // and it is what the map window and the report path both do with it. Here the
  // sentence was printed and then treated as fatal.
  //
  // Which meant the command refused to run in precisely the state every
  // download is in. Snapshots are fetched, never shipped, so a release archive
  // has none; all four scenarios that ship name one; so `paddock dashboard`
  // exited 1 on every scenario in every release, and only ever worked on a
  // machine that had already fetched LiDAR. The one build nobody tries it on is
  // a developer's.
  //
  // Slope changes growth, so a flat run is a different farm and the reason goes
  // to stderr where it will be read.
  if (const std::string trouble = paddock::app::attach_elevation(bundle, arguments.front());
      !trouble.empty()) {
    std::cerr << "paddock: " << trouble
              << "\n         The indicators below are for that flat farm.\n";
  }
  if (!bundle.management.has_value()) {
    std::cerr << "paddock: '" << bundle.name
              << "' has no [management] section, so it has no stock and half these indicators "
                 "would be blank"
                 "\n";
    return 2;
  }

  // **The zone rule is an argument, not a default.** New Zealand has no national
  // nitrogen loss limit - councils set them catchment by catchment - so a
  // dashboard that reached for one on its own would be asserting that this farm
  // is in that zone. Without --rule the nitrogen panel still reports what
  // leached; it simply has nothing to say about compliance, which is the honest
  // answer when nobody has said which rule applies.
  std::optional<paddock::config::NitrogenRegulation> rule;
  if (!rule_path.empty()) {
    rule = paddock::config::load_nitrogen_regulation(rule_path);
  }

  // **Without --economics the money and flock panels are simply absent**, which
  // is better than showing a farm's finances against costs nobody stated. The
  // pasture, water and nitrogen panels do not need them.
  std::optional<paddock::config::FarmEconomics> economics;
  if (!economics_path.empty()) {
    economics = paddock::config::load_economics(economics_path);
  }

  // What the stock get out of the grass, as the bundle says. It used to be a
  // constant here and in two other places.
  const paddock::core::DietQuality diet = bundle.diet;

  const auto board_for = [&](const paddock::config::ScenarioBundle& one, const std::string& label) {
    if (economics.has_value()) {
      return paddock::config::build_dashboard(
          one,
          paddock::config::run_managed_scenario(one, *one.management, diet, label,
                                                paddock::config::business_from(one, *economics)),
          label, rule);
    }
    return paddock::config::build_dashboard(
        one, paddock::config::run_managed_scenario(one, *one.management, diet, label), label, rule);
  };

  std::vector<paddock::config::FarmDashboard> boards;
  if (years.empty()) {
    boards.push_back(board_for(bundle, bundle.range.first.to_iso_string().substr(0, 4)));
  } else {
    for (const int start : years) {
      paddock::config::ScenarioBundle one = bundle;
      one.range = paddock::core::DateRange{paddock::core::Date{start, 7, 1},
                                           paddock::core::Date{start + 1, 6, 30}};
      boards.push_back(board_for(one, std::to_string(start) + "-" +
                                          (((start + 1) % 100) < 10 ? "0" : "") +
                                          std::to_string((start + 1) % 100)));
    }
  }

  if (boards.size() == 1) {
    std::cout << paddock::config::as_text(boards.front());
  } else {
    std::cout << paddock::config::compare_dashboards_as_text(boards);
  }

  if (!csv_stem.empty()) {
    std::ofstream indicators(csv_stem + "-indicators.csv");
    indicators << (boards.size() == 1 ? paddock::config::indicators_as_csv(boards.front())
                                      : paddock::config::compare_dashboards_as_csv(boards));
    std::ofstream series(csv_stem + "-series.csv");
    series << paddock::config::series_as_csv(boards.front());
    std::cout << "\n"
                 "  wrote "
              << csv_stem << "-indicators.csv and " << csv_stem
              << "-series.csv"
                 "\n";
  }
  return 0;
}

int run_nitrogen(const std::vector<std::string>& arguments) {
  paddock::config::ScenarioBundle bundle = paddock::config::load_scenario(arguments.front());

  // Flat rather than refused, for the reason set out in run_dashboard. The
  // comment this replaces claimed to be "the same attachment `scenario run`
  // makes" - and `scenario run` makes no such attachment. It builds a farmlet,
  // which is a point model with no ground under it at all.
  //
  // **Leaching is the indicator where flat ground is a real omission**, which
  // is why it is worth saying rather than only logging. Leaching follows
  // drainage, and on a slope some of the water that would have drained runs off
  // instead - so a flat farm over-drains and this report reads high. That is
  // the safe direction for a compliance report and it is still not the farm's
  // number.
  if (const std::string trouble = paddock::app::attach_elevation(bundle, arguments.front());
      !trouble.empty()) {
    std::cerr << "paddock: " << trouble
              << "\n         Leaching is reported for that flat farm, and flat "
                 "ground drains where a slope would shed, so this reads high.\n";
  }
  const paddock::config::NitrogenRegulation rule =
      paddock::config::load_nitrogen_regulation(arguments[1]);

  if (!bundle.management.has_value()) {
    std::cerr << "paddock: '" << bundle.name
              << "' has no [management] section, so it has no stock to graze it - and on a "
                 "New Zealand farm it is the stock's excreta, not fertiliser, that leaches\n";
    return 2;
  }

  // What the stock get out of the grass, as the bundle says. It used to be a
  // constant here and in two other places.
  const paddock::core::DietQuality diet = bundle.diet;

  const auto year_of = [&bundle, &diet](int start) {
    paddock::config::ScenarioBundle one = bundle;
    one.range = paddock::core::DateRange{paddock::core::Date{start, 7, 1},
                                         paddock::core::Date{start + 1, 6, 30}};
    const std::string label = std::to_string(start) + "-" + (start % 100 + 1 < 10 ? "0" : "") +
                              std::to_string((start + 1) % 100);
    return paddock::config::nitrogen_year(
        paddock::config::run_managed_scenario(one, *one.management, diet, label), label);
  };

  std::vector<paddock::config::NitrogenYear> years;
  if (arguments.size() <= 2) {
    const std::string label =
        bundle.range.first.to_iso_string() + " to " + bundle.range.last.to_iso_string();
    years.push_back(paddock::config::nitrogen_year(
        paddock::config::run_managed_scenario(bundle, *bundle.management, diet, label), label));
  } else {
    for (std::size_t i = 2; i < arguments.size(); ++i) {
      years.push_back(year_of(std::stoi(arguments[i])));
    }
  }

  std::cout << bundle.name << " - nitrogen loss to water\n\n";
  if (years.size() == 1) {
    std::cout << paddock::config::nitrogen_compliance_report(years.front(), rule);
  } else {
    std::cout << paddock::config::nitrogen_years_report(years, rule);
  }
  return 0;
}

#if defined(PADDOCK_WITH_CONFIG) && defined(PADDOCK_WITH_GIS)
/// `paddock ground fetch <bundle>` - puts the ground a scenario names on this
/// machine.
///
/// **This exists so that measured terrain is not an installation step.** Every
/// download of this simulator arrives without ground: snapshots are bulk, they
/// go stale, and the directory they live in is shared with sources whose
/// licences forbid passing them on at all, so nothing in data/snapshots/ ever
/// ships. Until now the instruction was to run a Python script, which asks
/// somebody who wants to look at a farm to install a language runtime first.
///
/// It fetches one known address and checks it against a hash the bundle decided
/// on before any request went out. It does not search: finding which of
/// thousands of tiles covers a farm is what scripts/nz-elevation-snapshot.py is
/// for, and it is something you do once, when you decide where the farm is.
int run_ground_fetch(const std::string& bundle_directory) {
  const paddock::config::ScenarioBundle bundle = paddock::config::load_scenario(bundle_directory);

  if (bundle.terrain.is_flat()) {
    std::cout << bundle.name << " is modelled on flat ground and names no elevation to fetch.\n";
    return 0;
  }
  if (!bundle.terrain.is_fetchable()) {
    std::cerr << "paddock: '" << bundle.name
              << "' names an elevation file but not where it is published, so there is nothing "
                 "to fetch it from. Add a 'url' and an 'attribution' to [terrain], or fetch it "
                 "with scripts/nz-elevation-snapshot.py.\n";
    return 2;
  }

  const std::filesystem::path destination =
      (std::filesystem::path(bundle_directory) / bundle.terrain.elevation_path).lexically_normal();

  // **Already here is a success, not a no-op to report as one.** The hash is
  // what decides that, not the file's presence: something at that path which is
  // not this scenario's ground would otherwise be left in place by a command
  // whose whole job is to put the right file there.
  if (std::ifstream existing(destination, std::ios::binary); existing) {
    const std::string contents((std::istreambuf_iterator<char>(existing)),
                               std::istreambuf_iterator<char>());
    if (paddock::core::Sha256::hex_of(contents) == bundle.terrain.elevation_sha256) {
      std::cout << "The ground for " << bundle.name << " is already here and is the right file.\n"
                << "  " << destination.string() << '\n';
      return 0;
    }
    std::cout << "There is a file at " << destination.string()
              << " and it is not this scenario's ground. Fetching the right one over it.\n";
  }

  std::cout << "Fetching the ground for " << bundle.name << "\n"
            << "  from " << bundle.terrain.elevation_url << "\n"
            << "  to   " << destination.string() << "\n"
            << "\n"
            << bundle.terrain.elevation_attribution << "\n\n";

  paddock::gis::ElevationDownload request;
  request.url = bundle.terrain.elevation_url;
  request.destination = destination.string();
  request.expected_sha256 = bundle.terrain.elevation_sha256;

  // Progress on one line that rewrites itself, and only when the size is known.
  // A percentage the server never gave would be a made-up number in a project
  // that spends most of its comments not making numbers up.
  std::int64_t reported = -1;
  const paddock::gis::DownloadOutcome outcome = paddock::gis::download_elevation(
      request, [&reported](std::int64_t so_far, std::int64_t total) {
        if (total > 0) {
          const std::int64_t percent = so_far * 100 / total;
          if (percent != reported) {
            reported = percent;
            std::cout << "\r  " << percent << "% of " << (total / (1024 * 1024)) << " MB"
                      << std::flush;
          }
        }
        return true;
      });
  if (reported >= 0) {
    std::cout << '\n';
  }

  if (!outcome.ok) {
    std::cerr << "paddock: " << outcome.reason << '\n';
    return 1;
  }

  std::cout << "  " << (outcome.bytes / (1024 * 1024)) << " MB, sha256 " << outcome.sha256
            << "\n\nThis scenario will now run on its measured ground.\n";
  return 0;
}
#endif

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

/// The last word before the process ends, and it must not itself fail: a throw
/// from inside one of main's handlers is not caught by the sibling handlers, so
/// it leaves main and the process dies in std::terminate having printed
/// nothing - the exact outcome those handlers exist to prevent. Hence C stdio,
/// which does not throw, with `noexcept` to hold this to it.
///
/// This is not hypothetical: it is what clang-tidy's bugprone-exception-escape
/// reports here against the MSVC standard library, whose ostream insertion
/// carries a visible throw path. libstdc++ keeps its equivalent out of line, so
/// the Linux CI gate says nothing about this file. Do not expect CI to notice
/// if it comes back.
void report_fatal(const char* message) noexcept {
  std::fputs("paddock: ", stderr);
  std::fputs(message, stderr);
  std::fputc('\n', stderr);
}

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
    if (args.size() >= 3 && args[0] == "disease") {
      std::vector<std::string> rest;
      rest.reserve(args.size() - 1);
      for (std::size_t i = 1; i < args.size(); ++i) {
        rest.emplace_back(args[i]);
      }
      return run_disease(rest);
    }

    if (args.size() >= 2 && args[0] == "dashboard") {
      std::vector<std::string> rest;
      rest.reserve(args.size() - 1);
      for (std::size_t i = 1; i < args.size(); ++i) {
        rest.emplace_back(args[i]);
      }
      return run_dashboard(rest);
    }

    if (args.size() >= 3 && args[0] == "nitrogen") {
      std::vector<std::string> rest;
      rest.reserve(args.size() - 1);
      for (std::size_t i = 1; i < args.size(); ++i) {
        rest.emplace_back(args[i]);
      }
      return run_nitrogen(rest);
    }

#ifdef PADDOCK_WITH_GIS
    if (args.size() == 3 && args[0] == "ground" && args[1] == "fetch") {
      return run_ground_fetch(std::string(args[2]));
    }
#endif

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
    report_fatal(error.what());
    return 1;
  } catch (...) {
    // Something was thrown that does not derive from std::exception: a foreign
    // library's own type, or a bare `throw 42`. There is no interface to ask it
    // anything, so all that can honestly be said is that it happened - but that
    // is worth far more than the alternative, which is std::terminate ending
    // the process with no output at all and looking to the user like a crash.
    // The exit code is its own, so a script can tell this apart from a run that
    // failed for a reason we understood (1) or a mistyped command (2).
    //
    // paddock-gui says the same thing with the same code; see app/src/gui/main.cpp.
    report_fatal("an exception that is not a std::exception escaped; the run did not finish");
    return 3;
  }
}
