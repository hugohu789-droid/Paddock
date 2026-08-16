#include <exception>
#include <iostream>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include <paddock/core/SnapshotWeather.hpp>
#include <paddock/core/Version.hpp>
#include <paddock/core/Weather.hpp>

namespace {

void print_usage(std::ostream& out) {
  out << "paddock " << paddock::core::engine_version() << "\n\n"
      << "Usage:\n"
      << "  paddock --version                    Print the engine version\n"
      << "  paddock --help                       Print this message\n"
      << "  paddock source test <snapshot.csv>   Check a weather snapshot\n\n"
      << "Weather snapshots are produced by scripts/cliflo-snapshot.py. Synthetic\n"
      << "sites are configured in TOML and arrive with the config loader.\n";
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

    std::cerr << "paddock: unrecognised command\n";
    print_usage(std::cerr);
    return 2;
  } catch (const std::exception& error) {
    std::cerr << "paddock: " << error.what() << '\n';
    return 1;
  }
}
