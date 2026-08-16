// The part of the map view that can be quantitatively wrong.
//
// Rendering needs a graphics driver; deciding which value gets which colour does
// not. Everything here runs headless, which is why the mapping is a pure
// function with no Qt and no VTK in it (ADR 0010).

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdio>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <paddock/core/Raster.hpp>
#include <paddock/viz/ColourScale.hpp>

namespace paddock::viz {
namespace {

core::Raster<double> raster_of(std::vector<double> values) {
  core::GeoTransform transform;
  transform.origin_easting = 1570000.0;
  transform.origin_northing = 5180000.0;
  core::Raster<double> raster(values.size(), 1, transform);
  for (std::size_t i = 0; i < values.size(); ++i) {
    raster(i, 0) = values[i];
  }
  return raster;
}

/// Perceived lightness, near enough for asserting that a ramp gets lighter.
double luminance(const Rgb& colour) {
  return (0.2126 * colour.red) + (0.7152 * colour.green) + (0.0722 * colour.blue);
}

TEST(ColourScaleTest, TheEndsOfTheRangeGiveTheEndsOfTheRamp) {
  const ColourScale scale(Ramp::Viridis, 0.0, 100.0);

  // Viridis begins at dark purple and ends at yellow.
  EXPECT_EQ(scale.colour_of(0.0), (Rgb{68, 1, 84}));
  EXPECT_EQ(scale.colour_of(100.0), (Rgb{253, 231, 37}));
}

// A value off the end of the scale must clamp. Wrapping, or going black, would
// make the map lie about its data.
TEST(ColourScaleTest, ValuesOutsideTheRangeClamp) {
  const ColourScale scale(Ramp::Viridis, 0.0, 100.0);

  EXPECT_EQ(scale.colour_of(-50.0), scale.colour_of(0.0));
  EXPECT_EQ(scale.colour_of(1e9), scale.colour_of(100.0));
  EXPECT_EQ(scale.colour_of(-1e9), scale.colour_of(0.0));
}

TEST(ColourScaleTest, ViridisGetsLighterAcrossTheRange) {
  const ColourScale scale(Ramp::Viridis, 0.0, 1.0);

  double previous = -1.0;
  for (int step = 0; step <= 20; ++step) {
    const double value = static_cast<double>(step) / 20.0;
    const double current = luminance(scale.colour_of(value));
    EXPECT_GT(current, previous) << "at " << value;
    previous = current;
  }
}

// The farm map convention runs the other way: more feed is darker green.
TEST(ColourScaleTest, PastureGreenGetsDarkerAsCoverRises) {
  const ColourScale scale(Ramp::PastureGreen, 1000.0, 3500.0);

  EXPECT_GT(luminance(scale.colour_of(1000.0)), luminance(scale.colour_of(3500.0)));
  EXPECT_GT(scale.colour_of(3500.0).green, scale.colour_of(3500.0).red);
}

TEST(ColourScaleTest, TheMappingIsMonotonicInValue) {
  const ColourScale scale(Ramp::Viridis, -20.0, 60.0);
  const Rgb low = scale.colour_of(-20.0);
  const Rgb middle = scale.colour_of(20.0);
  const Rgb high = scale.colour_of(60.0);

  EXPECT_NE(low, middle);
  EXPECT_NE(middle, high);
  EXPECT_LT(luminance(low), luminance(middle));
  EXPECT_LT(luminance(middle), luminance(high));
}

TEST(ColourScaleTest, ALookupTableSpansTheRampAtAnySize) {
  const ColourScale scale(Ramp::Viridis, 0.0, 10.0);

  EXPECT_TRUE(scale.lookup_table(0).empty());
  EXPECT_EQ(scale.lookup_table(1).size(), 1U);

  const std::vector<Rgb> table = scale.lookup_table(256);
  ASSERT_EQ(table.size(), 256U);
  EXPECT_EQ(table.front(), scale.colour_of(0.0));
  EXPECT_EQ(table.back(), scale.colour_of(10.0));
}

TEST(ColourScaleTest, AnInvertedRangeIsWidenedRatherThanDividedByZero) {
  const ColourScale inverted(Ramp::Viridis, 10.0, 5.0);
  const ColourScale empty(Ramp::Viridis, 3.0, 3.0);

  EXPECT_DOUBLE_EQ(inverted.maximum(), 11.0);
  EXPECT_DOUBLE_EQ(empty.maximum(), 4.0);
  EXPECT_EQ(empty.colour_of(3.0), empty.colour_of(3.0));
}

TEST(ColourScaleTest, TheRangeOfARasterCoversItsValues) {
  const std::pair<double, double> range =
      ColourScale::range_of(raster_of({1200.0, 3400.0, 2000.0, 900.0}));

  EXPECT_DOUBLE_EQ(range.first, 900.0);
  EXPECT_DOUBLE_EQ(range.second, 3400.0);
}

// A farm that has not been stepped yet is uniform. One flat colour is not
// wrong, but a division by zero is, so the range widens instead.
TEST(ColourScaleTest, AUniformRasterGetsAUsableRange) {
  const std::pair<double, double> uniform = ColourScale::range_of(raster_of({2000.0, 2000.0}));
  const std::pair<double, double> nothing = ColourScale::range_of(core::Raster<double>{});

  EXPECT_DOUBLE_EQ(uniform.first, 2000.0);
  EXPECT_DOUBLE_EQ(uniform.second, 2001.0);
  EXPECT_DOUBLE_EQ(nothing.first, 0.0);
  EXPECT_DOUBLE_EQ(nothing.second, 1.0);
}

TEST(ColourScaleTest, RampsAreNamedForTheLegend) {
  EXPECT_EQ(ramp_name(Ramp::Viridis), "viridis");
  EXPECT_EQ(ramp_name(Ramp::PastureGreen), "pasture green");
}

/// One label, printed the way vtkScalarBarActor prints it: through the C
/// formatting it is handed at run time.
///
/// -Wformat-nonliteral is off for exactly this call. The warning exists to
/// catch a format string that came from somewhere untrusted; here the format
/// is the return value of the function under test, and running it through
/// snprintf is the whole point - a format that snprintf cannot use is the
/// failure this file is meant to catch.
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#endif
std::string printed_with(const std::string& format, double value) {
  std::array<char, 64> buffer{};
  const int written = std::snprintf(buffer.data(), buffer.size(), format.c_str(), value);
  EXPECT_GT(written, 0) << "snprintf rejected the format " << format;
  return {buffer.data()};
}
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

/// The labels a legend would actually print, so a test can assert on the text
/// rather than on the format string that produced it.
std::vector<std::string> tick_labels(double low, double high, int labels) {
  const std::string format = tick_label_format(low, high, labels);
  std::vector<std::string> printed;
  for (int i = 0; i < labels; ++i) {
    const double value =
        low + (high - low) * static_cast<double>(i) / static_cast<double>(labels - 1);
    printed.push_back(printed_with(format, value));
  }
  return printed;
}

TEST(ColourScaleTest, LegendLabelsAreDistinctAcrossEveryFieldsRange) {
  // The ranges the four map fields actually take over a Canterbury year. The
  // regression this guards: a single fixed "%.0f" printed legume fraction as
  // five copies of the same digit, and a legend whose labels are all equal is
  // worse than no legend, because it looks like data.
  const std::vector<std::pair<double, double>> ranges = {
      {1900.0, 4560.0},   // pasture cover, kg DM/ha
      {21.4, 138.9},      // soil water, mm
      {0.0, 1.0},         // water stress, on its natural scale
      {0.1336, 0.1813}};  // legume fraction, over a run
  for (const std::pair<double, double>& range : ranges) {
    const std::vector<std::string> printed = tick_labels(range.first, range.second, 5);
    const std::set<std::string> distinct(printed.begin(), printed.end());
    EXPECT_EQ(distinct.size(), printed.size())
        << "range " << range.first << " to " << range.second << " printed a repeated label";
  }
}

TEST(ColourScaleTest, LabelPrecisionFollowsTheMagnitudeOfTheRange) {
  // Thousands of kg DM/ha do not need a decimal point; a range five hundredths
  // wide is nothing but decimal point.
  EXPECT_EQ(tick_label_format(1900.0, 4560.0, 5), "%.0f");
  EXPECT_EQ(tick_label_format(0.0, 1.0, 5), "%.2f");
  EXPECT_EQ(tick_label_format(0.1336, 0.1813, 5), "%.3f");
}

TEST(ColourScaleTest, ADegenerateRangeStillGivesAUsableFormat) {
  // A field that has not varied yet, and a range handed over backwards. Neither
  // has a step to read decimal places off, and neither may produce a format
  // that snprintf cannot use.
  for (const std::string& format : {tick_label_format(5.0, 5.0, 5), tick_label_format(9.0, 1.0, 5),
                                    tick_label_format(0.0, 1.0, 1)}) {
    EXPECT_FALSE(printed_with(format, 5.0).empty());
  }
}

}  // namespace
}  // namespace paddock::viz
