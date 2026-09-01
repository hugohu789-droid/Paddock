// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#pragma once

#include <optional>
#include <vector>

#include <paddock/core/SimulationClock.hpp>
#include <paddock/core/Weather.hpp>

namespace paddock::app {

/// Which farm years a weather source can run end to end.
///
/// **A farm year is not a calendar year**, and that is the whole of the
/// difficulty. It runs 1 July to 30 June, so a snapshot covering 2015-01-01 to
/// 2025-12-31 holds ten whole farm years and not eleven: the first opens in the
/// July after the file starts, and the last must close in a June the file still
/// reaches. Counting calendar years instead would offer a year at each end that
/// the run would then fail to fetch weather for.
///
/// **A generator has no particular years.** A synthetic source will make any
/// year asked of it and answers `covers()` with nothing, which is not the same
/// as covering nothing - so it gets an empty list here, and the caller says
/// there is nothing to compare rather than pretending there are no years.
///
/// Lives in a header of its own, beside AttachElevation.hpp and for the same
/// reason: it is the arithmetic behind a menu item, it is easy to get wrong at
/// the ends, and a slot on a window is not a thing a test can call.
[[nodiscard]] inline std::vector<int> whole_farm_years(const core::WeatherSource& weather) {
  const std::optional<core::DateRange> covered = weather.covers();
  if (!covered.has_value()) {
    return {};
  }

  std::vector<int> years;
  for (int start = covered->first.year; start <= covered->last.year; ++start) {
    const core::Date opens{start, 7, 1};
    const core::Date closes{start + 1, 6, 30};
    // Inclusive at both ends: a file that begins exactly on 1 July holds that
    // year, and one that ends exactly on 30 June holds the year closing on it.
    if (!(opens < covered->first) && !(covered->last < closes)) {
      years.push_back(start);
    }
  }
  return years;
}

}  // namespace paddock::app
