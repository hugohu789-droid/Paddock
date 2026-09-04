// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#ifndef PADDOCK_TESTS_SUPPORT_WINCHMORE_RESPONSE_HPP
#define PADDOCK_TESTS_SUPPORT_WINCHMORE_RESPONSE_HPP

#include <algorithm>
#include <cstddef>
#include <numeric>
#include <string>
#include <vector>

#include "CalibrationTable.hpp"

/// What irrigation measurably did at Winchmore, year by year.
///
/// **The trial's irrigated treatments, which sat in the repository unread.**
/// `winchmore-annual-production.csv` carries `dryland` beside `irrigated_10pc`,
/// `irrigated_15pc` and `irrigated_20pc` for the same twenty-five farm years,
/// and until E88 nothing had used the irrigated columns for anything. They are
/// the only external check this project has on what irrigation does, as opposed
/// to what a rain-fed farm produces.
///
/// **Read rather than transcribed.** `CanterburyDrylandTest` pins the dryland
/// mean and range as constants with a provenance comment, which was right when
/// three numbers were wanted; this needs a distribution, and a distribution
/// copied by hand into a header is a distribution that goes stale silently.
namespace paddock::tests {

/// The measured response to irrigation, as ratios of irrigated to rain-fed
/// production in the same year.
///
/// **Per-year ratios, not a ratio of means.** Winchmore's dryland column swings
/// from 3,904 to 9,845 kg DM/ha, so a wet year and a dry year answer irrigation
/// very differently; dividing the two means would hide exactly the spread a
/// model has to be allowed to move within.
class WinchmoreResponse {
 public:
  /// `columns` names the treatments to pool. The demo waters at FAO-56's
  /// p = 0.6 - as soon as the sward would be held back - which is a wet regime,
  /// so the 15% and 20% soil-moisture treatments are its company and the 10%,
  /// which lets the profile run much drier, is not.
  WinchmoreResponse(const std::string& path, const std::vector<std::string>& columns) {
    const test::CalibrationTable table(path);
    for (std::size_t row = 0; row < table.size(); ++row) {
      const double dryland = table.number(row, "dryland");
      if (dryland <= 0.0) {
        continue;
      }
      for (const std::string& column : columns) {
        ratios_.push_back(table.number(row, column) / dryland);
      }
    }
    std::sort(ratios_.begin(), ratios_.end());
  }

  [[nodiscard]] const std::vector<double>& ratios() const noexcept { return ratios_; }

  [[nodiscard]] std::size_t treatment_years() const noexcept { return ratios_.size(); }

  [[nodiscard]] double mean() const {
    return ratios_.empty() ? 0.0
                           : std::accumulate(ratios_.begin(), ratios_.end(), 0.0) /
                                 static_cast<double>(ratios_.size());
  }

  /// Linear interpolation between order statistics.
  ///
  /// **A description of this sample, not an estimate of a population.** Fifty
  /// treatment-years is not enough to fit a distribution to, and saying "p10"
  /// of a sorted list of fifty is an honest sentence where "the 10th percentile
  /// of Canterbury irrigation responses" would not be.
  [[nodiscard]] double percentile(double share) const {
    if (ratios_.empty()) {
      return 0.0;
    }
    const double position = (static_cast<double>(ratios_.size()) - 1.0) * share / 100.0;
    const auto below = static_cast<std::size_t>(position);
    const std::size_t above = std::min(below + 1, ratios_.size() - 1);
    return ratios_[below] +
           ((ratios_[above] - ratios_[below]) * (position - static_cast<double>(below)));
  }

  /// Where a model's ratio falls among the measured ones, as a percentage of
  /// treatment-years it equals or exceeds.
  ///
  /// **Reported, never gated.** Fifty treatment-years from one trial on one
  /// soil is a sample, and a pass condition written on a rank would be a much
  /// stronger claim about that sample than it can carry.
  [[nodiscard]] double percentile_of(double ratio) const {
    if (ratios_.empty()) {
      return 0.0;
    }
    const auto at_or_below = static_cast<double>(
        std::count_if(ratios_.begin(), ratios_.end(), [ratio](double r) { return r <= ratio; }));
    return 100.0 * at_or_below / static_cast<double>(ratios_.size());
  }

 private:
  std::vector<double> ratios_;
};

/// The pair the flagship demonstration is measured against.
[[nodiscard]] inline WinchmoreResponse winchmore_wet_treatments(const std::string& data_dir) {
  return WinchmoreResponse(data_dir + "/calibration/winchmore-annual-production.csv",
                           {"irrigated_15pc", "irrigated_20pc"});
}

}  // namespace paddock::tests

#endif  // PADDOCK_TESTS_SUPPORT_WINCHMORE_RESPONSE_HPP
