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
#include <QTabWidget>
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
#include "Theme.hpp"
#include "TrendDialog.hpp"

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
            << "  --irrigate     Turn irrigation on, as the panel would\n"
            << "  --inspect      Report the paddock under three points on screen\n"
            << "  --day N        Move the timeline to day N before drawing\n"
            << "  --pan N        Slide the terrain view, -100 to 100\n"
            << "  --layers       Show every layer of the scene\n"
            << "  --report-pdf F   Write this run's report to a PDF\n"
            << "  --compare      Run a rain-fed and an irrigated scenario and print the table\n"
            << "  --trends       Report the whole farm years this bundle's weather holds,\n"
            << "                 run two of them and build the years page\n"
            << "  --window-shot F  Save the whole window, controls included\n"
            << "  --field NAME   Draw a named field, as the list under the map does\n"
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
    // --inspect is on this list because it reports to the console and then has
    // nothing left to do: without it the window opens and waits for somebody,
    // and a diagnostic that blocks is not one.
    const bool smoke = !screenshot.empty() ||
                       std::find(args.begin(), args.end(), "--panel-shot") != args.end() ||
                       std::find(args.begin(), args.end(), "--inspect") != args.end() ||
                       std::find(args.begin(), args.end(), "--compare") != args.end() ||
                       std::find(args.begin(), args.end(), "--trends") != args.end() ||
                       std::find(args.begin(), args.end(), "--role") != args.end() ||
                       std::find(args.begin(), args.end(), "--report-pdf") != args.end() ||
                       std::find(args.begin(), args.end(), "--window-shot") != args.end() ||
                       std::find(args.begin(), args.end(), "--smoke") != args.end();
    // Must be set before the QApplication exists, or the widget and the render
    // window disagree about the surface they share.
    QSurfaceFormat::setDefaultFormat(QVTKOpenGLNativeWidget::defaultFormat());
    QApplication application(argc, argv);
    paddock::app::apply_theme(application);

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

    const bool irrigate = std::find(args.begin(), args.end(), "--irrigate") != args.end();
    const auto ground_flag = std::find(args.begin(), args.end(), "--ground");
    const bool terrain = std::find(args.begin(), args.end(), "--terrain") != args.end();
    const bool heights_given = std::find(args.begin(), args.end(), "--heights") != args.end();
    if (ground_flag != args.end() || terrain || heights_given || irrigate) {
      int ground = 0;
      if (ground_flag != args.end() && std::next(ground_flag) != args.end()) {
        ground = std::stoi(std::string(*std::next(ground_flag)));
      }
      int heights = 1;
      const auto heights_flag = std::find(args.begin(), args.end(), "--heights");
      if (heights_flag != args.end() && std::next(heights_flag) != args.end()) {
        heights = std::stoi(std::string(*std::next(heights_flag)));
      }
      window.show_configuration(ground, terrain, heights, irrigate);
      window.wait_for_run();
    }

    if (smoke) {
      // A run is on a worker now, so a batch mode has to wait for it before it
      // reads anything. A person clicking never needs this; a script always
      // does.
      window.wait_for_run();
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
      // How steep the farm is, which the picture cannot say.
      {
        const std::pair<double, double> slope =
            window.field_range(paddock::app::MapWindow::Field::Slope);
        std::cout << "paddock-gui: slope " << slope.first << " to " << slope.second << " degrees\n";
      }
      if (const paddock::core::IrrigationTally& water = window.irrigation(); water.events > 0) {
        std::cout << "paddock-gui: irrigated " << water.effective_mm << " mm over " << water.events
                  << " events, " << water.mean_event_mm() << " mm a time" << '\n';
      }
      if (const std::string& reason = window.no_ground_reason(); !reason.empty()) {
        std::cout << "paddock-gui: " << reason << '\n';
      }
      // Changing farms, which no single frame can check. Two bundles kilometres
      // apart used to leave the camera on the first one, so the second rendered
      // as an empty window - a failure that looks exactly like a broken
      // pipeline and is not one.
      // The window must not resize as the run plays.
      //
      // The weather and summary lines change length every day - "dry" becomes
      // "12.3 mm", the ground note appears and disappears - and a label whose
      // size hint reaches the layout drags the window with it. A screenshot
      // cannot show that; it takes two days and a comparison.
      if (window.day_count() > 1) {
        const int opening = window.width();
        int widest = opening;
        int narrowest = opening;
        const std::size_t days = window.day_count();
        for (std::size_t sample = 0; sample < 24; ++sample) {
          window.show_day_for_check(static_cast<int>((sample * days) / 24));
          widest = std::max(widest, window.width());
          narrowest = std::min(narrowest, window.width());
        }
        if (widest != narrowest) {
          std::cerr << "paddock-gui: the window resized while playing, from " << narrowest << " to "
                    << widest << " pixels. A status line is sizing the layout.\n";
          return 1;
        }
        std::cout << "paddock-gui: window steady at " << opening << " pixels across the year\n";
      }

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
        window.wait_for_run();

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

      // Which field to draw, so that a map mode is reachable by something
      // other than a person clicking.
      if (const auto field_flag = std::find(args.begin(), args.end(), "--field");
          field_flag != args.end() && std::next(field_flag) != args.end()) {
        const std::string field(*std::next(field_flag));
        if (!window.select_field(field)) {
          std::cerr << "paddock-gui: no field called '" << field << "'" << '\n';
          return 2;
        }
        std::cout << "paddock-gui: showing " << field << '\n';
      }

      // **A comparison, end to end, from the command line.**
      //
      // The largest thing this window does - five runs, a table of differences
      // and a paragraph about them - is the one thing no screenshot of the map
      // can show. Two scenarios that differ only in whether the farm irrigates
      // is the comparison the project exists to make, so it is the one checked:
      // if irrigation stops changing what the farm grows, this says so.
      if (std::find(args.begin(), args.end(), "--compare") != args.end()) {
        window.select_irrigation(false);
        window.keep_scenario("Rain-fed");
        window.select_irrigation(true);
        window.keep_scenario("Irrigated");

        QString why;
        const std::string table = window.comparison_markdown(why);
        if (table.empty()) {
          std::cerr << "paddock-gui: the comparison produced nothing: " << why.toStdString()
                    << '\n';
          return 1;
        }
        std::cout << table;
      }

      // **The years page, driven the way a person drives it.** The window has no
      // test binary of its own, so the way GUI work is checked here is a
      // headless run that exercises the same methods the menu item calls -
      // which is why open_trends() was split into the discovery and the runs
      // rather than doing both inside a slot.
      // Which view of the setup panel to draw. The panel shows the farmer's
      // controls by default; a researcher gets the model's assumptions as well,
      // and a compliance reader neither, because none of it is theirs to set.
      if (const auto role = std::find(args.begin(), args.end(), "--role");
          role != args.end() && std::next(role) != args.end()) {
        window.choose_role(std::stoi(std::string(*std::next(role))));
        std::cout << "paddock-gui: setup panel showing role " << *std::next(role) << '\n';
      }

      if (std::find(args.begin(), args.end(), "--trends") != args.end()) {
        const std::vector<int> years = window.years_available();
        std::cout << "paddock-gui: " << years.size() << " whole farm years";
        if (!years.empty()) {
          std::cout << ", " << years.front() << " to " << years.back();
        }
        std::cout << '\n';
        if (years.size() < 2) {
          std::cerr << "paddock-gui: this bundle's weather holds fewer than two whole farm "
                       "years, so there is nothing to compare\n";
          return 1;
        }

        // **Every year, because that is what the button does.** Two would prove
        // the page builds and would not prove the picture is right: the chart
        // draws one line per year on a shared scale, and a two-line version of
        // it is not the thing anybody looks at.
        std::string failure;
        std::vector<paddock::config::FarmDashboard> boards =
            window.run_year_boards(years, {}, failure);
        if (boards.size() != years.size()) {
          std::cerr << "paddock-gui: the years did not run: " << failure << '\n';
          return 1;
        }

        paddock::app::TrendDialog trends(std::move(boards));
        std::cout << "paddock-gui: the years page built\n";

        // A path after the flag saves the page, which is how it gets looked at
        // without a person at the screen - the same bargain --window-shot makes
        // for the map.
        if (const auto shot = std::find(args.begin(), args.end(), "--trends");
            std::next(shot) != args.end() && std::next(shot)->substr(0, 2) != "--") {
          const QString path = QString::fromStdString(std::string(*std::next(shot)));
          trends.show();
          QCoreApplication::processEvents();

          // **Every tab, not the one that happens to be in front.** The page is
          // four different pages, and a screenshot of the first says nothing
          // about whether the other three draw at all - which is exactly the
          // kind of fault that goes unnoticed until somebody clicks.
          auto* tabs = trends.findChild<QTabWidget*>();
          const int pages = tabs != nullptr ? tabs->count() : 1;
          for (int page = 0; page < pages; ++page) {
            if (tabs != nullptr) {
              tabs->setCurrentIndex(page);
            }
            QCoreApplication::processEvents();
            QString each = path;
            if (tabs != nullptr && pages > 1) {
              const int dot = static_cast<int>(each.lastIndexOf('.'));
              const QString stem = dot > 0 ? each.left(dot) : each;
              const QString suffix = dot > 0 ? each.mid(dot) : QString(".png");
              each = stem + "-" + tabs->tabText(page).toLower().replace(' ', '-') + suffix;
            }
            if (!trends.grab().save(each)) {
              std::cerr << "paddock-gui: could not write " << each.toStdString() << '\n';
              return 1;
            }
            std::cout << "paddock-gui: wrote " << each.toStdString() << '\n';
          }
        }
      }

      if (const auto pdf_flag = std::find(args.begin(), args.end(), "--report-pdf");
          pdf_flag != args.end() && std::next(pdf_flag) != args.end()) {
        const std::string pdf(*std::next(pdf_flag));
        // The reason comes back from the window rather than being guessed at
        // here: "could not write it" and "there is nothing worth writing" send
        // somebody looking in two different places.
        std::string failure;
        if (!window.save_run_pdf(pdf, failure)) {
          std::cerr << "paddock-gui: " << failure << '\n';
          return 1;
        }
        std::cout << "paddock-gui: wrote " << pdf << '\n';
      }

      // Which day to show, so a check can land on one where something
      // happened. Irrigation runs in the dry months and the timeline opens
      // wherever the farm varied most, which is not always the same day.
      if (const auto day_flag = std::find(args.begin(), args.end(), "--day");
          day_flag != args.end() && std::next(day_flag) != args.end()) {
        window.go_to_day(std::stoi(std::string(*std::next(day_flag))));
        std::cout << "paddock-gui: showing day " << *std::next(day_flag) << '\n';
      }

      if (std::find(args.begin(), args.end(), "--layers") != args.end()) {
        window.show_all_layers();
        std::cout << "paddock-gui: every layer shown" << '\n';

        // **The one part of the irrigation picture a screenshot cannot show.**
        // The pivots turn, and turning is the whole of what they say. Ticking
        // the layer once told the scene the spray was wanted without handing it
        // a day, so the arms stood still on exactly the day somebody had asked
        // to watch - which no image of the scene would have caught.
        if (window.irrigation_today_mm() > 0.0) {
          if (!window.irrigation_animating()) {
            std::cerr << "paddock-gui: this day was watered but the pivots are not turning" << '\n';
            return 1;
          }
          std::cout << "paddock-gui: the pivots are turning" << '\n';
        }
      }

      // How far to slide the view, so the control can be checked without a
      // person dragging it.
      if (const auto pan_flag = std::find(args.begin(), args.end(), "--pan");
          pan_flag != args.end() && std::next(pan_flag) != args.end()) {
        window.slide_view(std::stoi(std::string(*std::next(pan_flag))));
        std::cout << "paddock-gui: view slid " << *std::next(pan_flag) << " percent" << '\n';
      }

      // Inspecting a paddock, so the chain from a screen point to a paddock's
      // numbers can be checked by something other than a person clicking.
      //
      // **And checked, not merely exercised.** That chain - a screen pixel, the
      // ground under it, the paddock containing it, the cells that paddock owns
      // - has four places to get a coordinate frame wrong, and every one of
      // them returns a real paddock with believable numbers for the wrong piece
      // of ground. Qt measures y down and VTK measures it up, which is the
      // easiest of the four to get backwards and the hardest to notice.
      if (std::find(args.begin(), args.end(), "--inspect") != args.end()) {
        // **Checked on the flat map, whichever view is showing.**
        //
        // The window opens in three dimensions now, and there the farm sits in
        // the lower part of a frame that also holds the sky - so a point at the
        // middle of the window lands on nothing at all, and the north-west to
        // south-east test has no meaning under a camera carrying a bearing. The
        // flat map is drawn north up and fills its frame, which is the frame
        // this check is written for. Everything below ground_at is the same
        // code in both views.
        const bool was_terrain = window.showing_terrain();
        window.select_view(false);

        const int width = window.render_width();
        const int height = window.render_height();
        const auto at = [&window](double across, double up) {
          return std::pair<int, int>{static_cast<int>(window.render_width() * across),
                                     static_cast<int>(window.render_height() * up)};
        };
        // **Nearer the middle than the corners, because the map no longer has
        // the window to itself.** The chart and the readings took a share of it,
        // so the farm is drawn into a frame of a different shape and does not
        // reach the corners: at three tenths and seven tenths both probes landed
        // on the background and reported having missed the farm, which reads as
        // a broken inspector rather than as a farm that stops before the edge.
        // The check is unchanged in meaning - one point is still up and to the
        // left of the other, and still has to come back further north and
        // further west.
        const std::pair<int, int> north_west = at(0.40, 0.60);
        const std::pair<int, int> south_east = at(0.60, 0.40);

        for (const std::pair<const char*, std::pair<int, int>>& spot :
             {std::pair<const char*, std::pair<int, int>>{"centre", at(0.50, 0.50)},
              {"north-west", north_west},
              {"south-east", south_east}}) {
          std::cout << "paddock-gui: " << spot.first << " -> "
                    << window.inspect_pixel(spot.second.first, spot.second.second) << '\n';
        }

        // The flat map is drawn north up and east right, so a point up and to
        // the left of another must come back further north and further west.
        // In the terrain view the camera carries a bearing and this does not
        // hold, so it is not asserted there.
        if (width > 0 && height > 0) {
          const std::optional<paddock::core::Point2D> up_left =
              window.ground_under(north_west.first, north_west.second);
          const std::optional<paddock::core::Point2D> down_right =
              window.ground_under(south_east.first, south_east.second);
          if (!up_left.has_value() || !down_right.has_value()) {
            std::cerr << "paddock-gui: --inspect could not find the ground under the map" << '\n';
            return 1;
          }
          if (up_left->easting >= down_right->easting ||
              up_left->northing <= down_right->northing) {
            std::cerr << "paddock-gui: the map's north-west reads as " << up_left->easting << ", "
                      << up_left->northing << " and its south-east as " << down_right->easting
                      << ", " << down_right->northing
                      << " - the picked ground is mirrored, so a click reports the wrong paddock"
                      << '\n';
            return 1;
          }
          std::cout << "paddock-gui: the picked ground runs north-west to south-east correctly"
                    << '\n';
        }
        // Put the view back: a check must not decide what a screenshot shows.
        window.select_view(was_terrain);
      }

      // The day being shown, reported last on purpose.
      //
      // This line describes the day on screen, and several things above it move
      // that day: the width check plays the run, --then opens another farm, and
      // --day goes where it is told. Printed where it used to be - up with the
      // summary of the run - it named whichever day the timeline first opened on
      // and was then left behind, so the console described February while the
      // picture showed June. It misled twice before it was moved.
      if (const std::string& weather = window.weather_line(); !weather.empty()) {
        std::cout << "paddock-gui: weather " << weather << '\n';
      }

      if (const auto window_flag = std::find(args.begin(), args.end(), "--window-shot");
          window_flag != args.end() && std::next(window_flag) != args.end()) {
        const std::string window_path(*std::next(window_flag));
        if (!window.save_window_screenshot(window_path)) {
          std::cerr << "paddock-gui: could not write " << window_path << '\n';
          return 1;
        }
        std::cout << "paddock-gui: wrote " << window_path << '\n';
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
