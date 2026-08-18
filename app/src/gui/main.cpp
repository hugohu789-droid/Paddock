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
            << "                 at the screen\n";
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
    const bool smoke =
        !screenshot.empty() || std::find(args.begin(), args.end(), "--smoke") != args.end();
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

    if (smoke) {
      window.render_once();
      std::cout << "paddock-gui: rendered " << window.day_count() << " days of " << bundle.name
                << '\n';
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
    std::cerr << "paddock-gui: " << error.what() << '\n';
    return 1;
  }
}
