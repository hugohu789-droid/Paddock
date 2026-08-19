// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

// paddock-gui: the 2D map view.
//
// A separate executable from the `paddock` command line tool, so that a
// headless machine - a CI runner, a server running a parameter sweep - never
// links Qt or VTK at all.

#include <QApplication>
#include <QDir>
#include <QSurfaceFormat>
#include <QVTKOpenGLNativeWidget.h>
#include <algorithm>
#include <cstdio>
#include <exception>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include <paddock/config/ScenarioConfig.hpp>

#include "MapWindow.hpp"

namespace {

void print_usage() {
  std::cout << "paddock-gui <bundle> [--smoke] [--screenshot FILE]\n\n"
            << "  <bundle>       A scenario bundle directory with a [grid] section\n"
            << "  --smoke        Render one frame and exit; used by CI, which has\n"
            << "                 no one to click anything\n"
            << "  --screenshot   Write that frame to a PNG and exit. Implies --smoke,\n"
            << "                 and is how the map gets looked at without a person\n"
            << "                 at the screen\n"
            << "  --ground N     Run over the panel's Nth ground: 0 flat, 1 facing\n"
            << "                 north, 2 facing south, 3 rolling\n"
            << "  --terrain      Show the three-dimensional view rather than the\n"
            << "                 flat map\n"
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
    if (args.empty() || args.front() == "--help" || args.front() == "-h") {
      print_usage();
      return args.empty() ? 2 : 0;
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

    const paddock::config::ScenarioBundle bundle =
        paddock::config::load_scenario(std::string(args.front()));
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
    const QDir bundle_directory(QString::fromStdString(std::string(args.front())));
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
