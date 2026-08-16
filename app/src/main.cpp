#include <exception>
#include <iostream>
#include <ostream>
#include <string_view>
#include <vector>

#include <paddock/core/Version.hpp>

namespace {

void print_usage(std::ostream& out) {
  out << "paddock " << paddock::core::engine_version() << "\n\n"
      << "Usage:\n"
      << "  paddock --version    Print the engine version\n"
      << "  paddock --help       Print this message\n\n"
      << "Simulation commands (`source test`, `scenario run`) arrive with the\n"
      << "weather driver in M2.\n";
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

    std::cerr << "paddock: unknown argument '" << args.front() << "'\n";
    print_usage(std::cerr);
    return 2;
  } catch (const std::exception& error) {
    std::cerr << "paddock: " << error.what() << '\n';
    return 1;
  }
}
