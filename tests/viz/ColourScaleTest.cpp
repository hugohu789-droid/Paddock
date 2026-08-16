// The part of the map view that can be quantitatively wrong.
//
// Rendering needs a graphics driver; deciding which value gets which colour does
// not. Everything here runs headless, which is why the mapping is a pure
// function with no Qt and no VTK in it (ADR 0010).

#include <gtest/gtest.h>

#include <cstddef>
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

}  // namespace
}  // namespace paddock::viz
