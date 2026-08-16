#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <paddock/core/Raster.hpp>

namespace paddock::viz {

struct Rgb {
  std::uint8_t red = 0;
  std::uint8_t green = 0;
  std::uint8_t blue = 0;
};

[[nodiscard]] bool operator==(const Rgb& lhs, const Rgb& rhs) noexcept;
[[nodiscard]] bool operator!=(const Rgb& lhs, const Rgb& rhs) noexcept;

/// Which colours a map uses, and why there is a choice at all.
enum class Ramp : std::uint8_t {
  /// Viridis, sampled from matplotlib's published colormap. It is perceptually
  /// uniform - equal steps in value look like equal steps in colour - and
  /// remains readable to viewers with the common forms of colour blindness.
  /// This is the default for anything a reader might measure off the map.
  Viridis,

  /// Pale straw to dark green: the convention on farm pasture maps, where more
  /// green means more feed. Not perceptually uniform, and not safe for a
  /// red-green colour deficiency to read quantitatively, so it is offered for
  /// familiarity rather than for measurement.
  PastureGreen,
};

[[nodiscard]] std::string ramp_name(Ramp ramp);

/// A printf format for `labels` evenly spaced tick labels across [low, high],
/// with enough decimal places that consecutive labels differ.
///
/// One fixed format cannot serve every field in this model. Pasture cover runs
/// to thousands of kg DM/ha, where decimal places are noise; legume fraction
/// runs from 0.13 to 0.18, where "%.0f" prints the same digit five times and
/// the legend carries no information at all. The number of places is taken from
/// the step between labels - one place finer than the step - so a legend is
/// never a column of identical numbers.
[[nodiscard]] std::string tick_label_format(double low, double high, int labels);

/// Maps a value to a colour.
///
/// This is deliberately free of Qt and VTK. Which value gets which colour, what
/// happens outside the range, and what an unvarying raster does are the parts
/// that can be quantitatively wrong, and they are tested without a display or a
/// graphics driver.
class ColourScale {
 public:
  ColourScale(Ramp ramp, double minimum, double maximum);

  /// Values outside [minimum, maximum] clamp to the end colours. A map that
  /// silently wrapped or went black at the edges would be lying about its data.
  [[nodiscard]] Rgb colour_of(double value) const noexcept;

  /// `entries` colours evenly spaced across the range, for a lookup table or a
  /// legend.
  [[nodiscard]] std::vector<Rgb> lookup_table(std::size_t entries) const;

  [[nodiscard]] Ramp ramp() const noexcept { return ramp_; }

  [[nodiscard]] double minimum() const noexcept { return minimum_; }

  [[nodiscard]] double maximum() const noexcept { return maximum_; }

  /// The range covering a raster's values.
  ///
  /// A raster whose values are all equal - a farm that has not been stepped
  /// yet, or a field that is uniformly zero - has no range to speak of. Rather
  /// than divide by zero or show one flat colour, the range is widened by one
  /// unit so the map renders the value in the middle of the scale.
  [[nodiscard]] static std::pair<double, double> range_of(const core::Raster<double>& raster);

 private:
  Ramp ramp_;
  double minimum_;
  double maximum_;
};

}  // namespace paddock::viz
