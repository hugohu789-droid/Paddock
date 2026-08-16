// paddock-gui: the 2D map view.
//
// A separate executable from the `paddock` command line tool, so that a
// headless machine - a CI runner, a server running a parameter sweep - never
// links Qt or VTK at all.

#include <QApplication>
#include <QSurfaceFormat>
#include <QVTKOpenGLNativeWidget.h>
#include <algorithm>
#include <exception>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include <paddock/config/ScenarioConfig.hpp>

#include "MapWindow.hpp"

namespace {

void print_usage() {
  std::cout << "paddock-gui <bundle> [--smoke]\n\n"
            << "  <bundle>   A scenario bundle directory with a [grid] section\n"
            << "  --smoke    Render one frame and exit; used by CI, which has\n"
            << "             no one to click anything\n";
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const std::vector<std::string_view> args(argv + 1, argv + argc);
    if (args.empty() || args.front() == "--help" || args.front() == "-h") {
      print_usage();
      return args.empty() ? 2 : 0;
    }

    const bool smoke = std::find(args.begin(), args.end(), "--smoke") != args.end();
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
    paddock::app::MapWindow window(bundle);
    window.show();

    if (smoke) {
      window.render_once();
      std::cout << "paddock-gui: rendered " << window.day_count() << " days of " << bundle.name
                << '\n';
      return 0;
    }

    return QApplication::exec();
  } catch (const std::exception& error) {
    std::cerr << "paddock-gui: " << error.what() << '\n';
    return 1;
  }
}
