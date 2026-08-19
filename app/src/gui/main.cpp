// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

// paddock-gui: the 2D map view.
//
// A separate executable from the `paddock` command line tool, so that a
// headless machine - a CI runner, a server running a parameter sweep - never
// links Qt or VTK at all.

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QSurfaceFormat>
#include <QVTKOpenGLNativeWidget.h>
#include <algorithm>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <paddock/config/ScenarioConfig.hpp>

#include "MapWindow.hpp"

namespace {

/// The scenario the application opens on when it is given none. Lincoln
/// University's Research Dairy Farm is the one bundle in this repository built
/// from a real farm's published area and stocking, with a real LINZ elevation
/// snapshot behind it, so it is the one worth showing first.
constexpr const char* kDefaultBundle = "data/scenarios/lincoln-lurdf";

/// Find the default bundle without being told where the repository is.
///
/// **Why a search rather than a fixed path.** The executable is run three ways -
/// from the build directory by a developer, from the repository root by CI, and
/// from wherever a shortcut points - and none of them share a working directory.
/// Walking up from both the working directory and the executable covers all
/// three without an install step or an environment variable to forget. Returns
/// empty when nothing is found, and the caller says so rather than opening
/// something arbitrary.
std::string find_default_bundle() {
  std::vector<std::filesystem::path> starts;
  std::error_code error;
  const std::filesystem::path working = std::filesystem::current_path(error);
  if (!error) {
    starts.push_back(working);
  }
  const QString executable = QCoreApplication::applicationDirPath();
  if (!executable.isEmpty()) {
    starts.emplace_back(executable.toStdString());
  }

  for (const std::filesystem::path& start : starts) {
    for (std::filesystem::path directory = start; !directory.empty();
         directory = directory.parent_path()) {
      const std::filesystem::path candidate = directory / kDefaultBundle;
      if (std::filesystem::is_directory(candidate, error)) {
        return candidate.string();
      }
      if (!directory.has_relative_path()) {
        break;  // At the root, where parent_path() would spin.
      }
    }
  }
  return {};
}

void print_usage() {
  std::cout << "paddock-gui [bundle] [--smoke] [--screenshot FILE]\n\n"
            << "  [bundle]       A scenario bundle directory with a [grid] section.\n"
            << "                 Defaults to " << kDefaultBundle << ", found by\n"
            << "                 walking up from here and from the executable\n"
            << "  --smoke        Render one frame and exit; used by CI, which has\n"
            << "                 no one to click anything\n"
            << "  --screenshot   Write that frame to a PNG and exit. Implies --smoke,\n"
            << "                 and is how the map gets looked at without a person\n"
            << "                 at the screen\n"
            << "  --ground N     Run over the panel's Nth ground: 0 flat, 1 facing\n"
            << "                 north, 2 facing south, 3 rolling\n"
            << "  --terrain      Show the three-dimensional view rather than the\n"
            << "                 flat map\n"
            << "  --then BUNDLE  After the first frame, open BUNDLE as choosing it in the\n"
            << "                 panel would, and check the camera followed it onto the\n"
               "                 new farm. Exits non-zero if it did not\n"
            << "  --heights N    Stretch the terrain's heights N times. The factor stays\n"
            << "                 on screen, because exaggeration makes every slope look\n"
            << "                 steeper than it is\n";
}

/// Failure reporting that cannot itself fail, for the handlers in main. A throw
/// raised inside a handler is not caught by that handler's siblings: it escapes
/// main and the process ends in std::terminate with nothing printed. C stdio
/// does not throw, and `noexcept` holds this to it. The same reasoning, and the
/// reason CI cannot be relied on to catch a regression, is written out in
/// app/src/main.cpp.
void report_fatal(const char* message) noexcept {
  std::fputs("paddock-gui: ", stderr);
  std::fputs(message, stderr);
  std::fputc('\n', stderr);
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const std::vector<std::string_view> args(argv + 1, argv + argc);
    if (!args.empty() && (args.front() == "--help" || args.front() == "-h")) {
      print_usage();
      return 0;
    }

    const auto screenshot_flag = std::find(args.begin(), args.end(), "--screenshot");
    std::string screenshot;
    if (screenshot_flag != args.end()) {
      if (std::next(screenshot_flag) == args.end()) {
        std::cerr << "paddock-gui: --screenshot needs a file to write to\n";
        return 2;
      }
      screenshot = std::string(*std::next(screenshot_flag));
    }
    const bool smoke = !screenshot.empty() ||
                       std::find(args.begin(), args.end(), "--panel-shot") != args.end() ||
                       std::find(args.begin(), args.end(), "--smoke") != args.end();
    // Must be set before the QApplication exists, or the widget and the render
    // window disagree about the surface they share.
    QSurfaceFormat::setDefaultFormat(QVTKOpenGLNativeWidget::defaultFormat());
    const QApplication application(argc, argv);

    // A leading flag means no bundle was named, so the default stands in.
    std::string bundle_path;
    if (!args.empty() && args.front().substr(0, 2) != "--") {
      bundle_path = std::string(args.front());
    } else {
      bundle_path = find_default_bundle();
      if (bundle_path.empty()) {
        std::cerr << "paddock-gui: no scenario named, and " << kDefaultBundle
                  << " is not above this directory or the executable. Name a bundle "
                     "directory, or run from the repository.\n";
        return 2;
      }
    }

    const paddock::config::ScenarioBundle bundle = paddock::config::load_scenario(bundle_path);
    if (!bundle.grid.has_value()) {
      std::cerr << "paddock-gui: scenario '" << bundle.name
                << "' has no [grid] section, so there is no map to draw. Add one, or use "
                   "`paddock scenario run` for the single-hectare summary.\n";
      return 2;
    }
    // The setup panel offers every scenario and species it can find, and what
    // it looks in is the data directory the named bundle sits under:
    // data/scenarios/<bundle> means data/. Derived rather than asked for, so
    // the command line stays what it was and CI keeps working.
    const QDir bundle_directory(QString::fromStdString(bundle_path));
    QDir data_directory(bundle_directory);
    data_directory.cdUp();
    data_directory.cdUp();

    paddock::app::MapWindow window(bundle, bundle_directory.absolutePath().toStdString(),
                                   data_directory.absolutePath().toStdString());
    window.show();

    const auto ground_flag = std::find(args.begin(), args.end(), "--ground");
    const bool terrain = std::find(args.begin(), args.end(), "--terrain") != args.end();
    const bool heights_given = std::find(args.begin(), args.end(), "--heights") != args.end();
    if (ground_flag != args.end() || terrain || heights_given) {
      int ground = 0;
      if (ground_flag != args.end() && std::next(ground_flag) != args.end()) {
        ground = std::stoi(std::string(*std::next(ground_flag)));
      }
      int heights = 1;
      const auto heights_flag = std::find(args.begin(), args.end(), "--heights");
      if (heights_flag != args.end() && std::next(heights_flag) != args.end()) {
        heights = std::stoi(std::string(*std::next(heights_flag)));
      }
      window.show_configuration(ground, terrain, heights);
    }

    if (smoke) {
      if (const std::string& failure = window.last_failure(); !failure.empty()) {
        std::cerr << "paddock-gui: " << failure << '\n';
        return 1;
      }
      window.render_once();
      std::cout << "paddock-gui: rendered " << window.day_count() << " days of " << bundle.name
                << '\n';
      if (const auto ground = window.ground_range(); ground.has_value()) {
        std::cout << "paddock-gui: ground " << ground->first << " to " << ground->second
                  << " m above sea level\n";
      } else {
        std::cout << "paddock-gui: ground modelled flat\n";
      }
      if (const std::string& weather = window.weather_line(); !weather.empty()) {
        std::cout << "paddock-gui: weather " << weather << '\n';
      }
      if (const std::string& reason = window.no_ground_reason(); !reason.empty()) {
        std::cout << "paddock-gui: " << reason << '\n';
      }
      // Changing farms, which no single frame can check. Two bundles kilometres
      // apart used to leave the camera on the first one, so the second rendered
      // as an empty window - a failure that looks exactly like a broken
      // pipeline and is not one.
      const auto then_flag = std::find(args.begin(), args.end(), "--then");
      if (then_flag != args.end()) {
        if (std::next(then_flag) == args.end()) {
          std::cerr << "paddock-gui: --then needs a bundle to open" << '\n';
          return 2;
        }
        // No render_once here: it resets the camera itself, which would make
        // this check pass whether or not opening a farm moves the view. The run
        // draws its own frame.
        window.open_scenario(std::string(*std::next(then_flag)));

        const auto farm = window.drawn_farm();
        const auto focus = window.camera_focus();
        if (!farm.has_value() || !focus.has_value()) {
          std::cerr << "paddock-gui: --then drew no farm to look at" << '\n';
          return 1;
        }
        const double west = (*farm)[0];
        const double south = (*farm)[1];
        const double east = west + (*farm)[2];
        const double north = south + (*farm)[3];
        std::cout << "paddock-gui: camera on " << focus->first << ", " << focus->second << "; farm "
                  << west << " to " << east << " E, " << south << " to " << north << " N" << '\n';
        if (focus->first < west || focus->first > east || focus->second < south ||
            focus->second > north) {
          std::cerr << "paddock-gui: the camera stayed on the farm that was there before, so the "
                       "new one is off screen"
                    << '\n';
          return 1;
        }
      }

      const auto panel_flag = std::find(args.begin(), args.end(), "--panel-shot");
      if (panel_flag != args.end() && std::next(panel_flag) != args.end()) {
        const std::string panel_path(*std::next(panel_flag));
        if (!window.save_panel_screenshot(panel_path)) {
          std::cerr << "paddock-gui: could not write " << panel_path << '\n';
          return 1;
        }
        std::cout << "paddock-gui: wrote " << panel_path << '\n';
      }
      if (!screenshot.empty()) {
        if (!window.save_screenshot(screenshot)) {
          std::cerr << "paddock-gui: could not write " << screenshot << '\n';
          return 1;
        }
        std::cout << "paddock-gui: wrote " << screenshot << '\n';
      }
      return 0;
    }

    return QApplication::exec();
  } catch (const std::exception& error) {
    report_fatal(error.what());
    return 1;
  } catch (...) {
    // A throw of something outside the std::exception hierarchy - Qt and VTK
    // are C++ libraries with their own ideas, and either could grow one - would
    // otherwise reach std::terminate, which prints nothing and, in a windowed
    // program, means the window simply vanishes. Nothing can be asked of the
    // value, so the report says only that it happened, and the exit code is the
    // same 3 the `paddock` command line tool uses for the same situation.
    report_fatal("an exception that is not a std::exception escaped; the run did not finish");
    return 3;
  }
}
