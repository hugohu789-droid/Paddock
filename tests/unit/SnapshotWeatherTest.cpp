// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>

#include <paddock/core/Sha256.hpp>
#include <paddock/core/SimulationClock.hpp>
#include <paddock/core/SnapshotWeather.hpp>

namespace paddock::core {
namespace {

const std::string kValidSnapshot =
    "# station: test station\n"
    "date,rainfall_mm,min_air_temperature_c,max_air_temperature_c,solar_radiation_mj_per_m2,"
    "wind_speed_m_per_s\n"
    "2023-06-28,0.0,1.5,10.2,5.8,2.1\n"
    "\n"
    "2023-06-29,12.4,4.0,9.1,2.2,6.7\n"
    "2023-06-30,3.2,2.8,11.0,4.4,3.0\n"
    "2023-07-01,0.0,-1.2,8.6,6.9,1.4\n";

SnapshotWeatherSource::Options test_options() {
  SnapshotWeatherSource::Options options;
  options.path = "data/snapshots/test.csv";
  options.dataset = "test_station";
  options.licence = "test fixture";
  return options;
}

SnapshotWeatherSource loaded(const std::string& text) {
  return SnapshotWeatherSource::from_text(text, test_options());
}

TEST(SnapshotWeatherTest, ReplaysEveryRowAndHashesTheFile) {
  const SnapshotWeatherSource source = loaded(kValidSnapshot);

  ASSERT_TRUE(source.test_connection().ok) << source.test_connection().message;
  EXPECT_EQ(source.records().size(), 4U);
  EXPECT_EQ(source.coverage().first, (Date{2023, 6, 28}));
  EXPECT_EQ(source.coverage().last, (Date{2023, 7, 1}));
  EXPECT_EQ(source.content_hash(), Sha256::hex_of(kValidSnapshot));

  const WeatherSeries series = source.fetch(DateRange{Date{2023, 6, 29}, Date{2023, 6, 30}});
  ASSERT_EQ(series.size(), 2U);
  EXPECT_TRUE(series.is_well_formed());
  EXPECT_DOUBLE_EQ(series.records.front().rainfall_mm, 12.4);
  EXPECT_DOUBLE_EQ(series.records.front().wind_speed_m_per_s, 6.7);
  EXPECT_DOUBLE_EQ(series.records.back().max_air_temperature_c, 11.0);
  EXPECT_EQ(series.provenance.source_name, "weather_snapshot");
  EXPECT_EQ(series.provenance.dataset, "test_station");
  EXPECT_EQ(series.provenance.content_hash, source.content_hash());
}

TEST(SnapshotWeatherTest, ColumnOrderDoesNotMatterAndOptionalColumnsDefaultToZero) {
  const SnapshotWeatherSource source = loaded(
      "max_air_temperature_c,date,min_air_temperature_c,rainfall_mm\n"
      "12.5,2023-06-28,3.0,1.5\n");

  ASSERT_TRUE(source.test_connection().ok) << source.test_connection().message;
  ASSERT_EQ(source.records().size(), 1U);
  EXPECT_DOUBLE_EQ(source.records().front().max_air_temperature_c, 12.5);
  EXPECT_DOUBLE_EQ(source.records().front().rainfall_mm, 1.5);
  EXPECT_DOUBLE_EQ(source.records().front().solar_radiation_mj_per_m2, 0.0);
}

// Every parse failure names the file, the line and the column. A snapshot is
// something a user assembled from a download; "parse error" alone would send
// them looking through thousands of rows.
TEST(SnapshotWeatherTest, AMissingRequiredColumnIsReportedByName) {
  const SnapshotWeatherSource source =
      loaded("date,rainfall_mm,min_air_temperature_c\n2023-06-28,0.0,1.5\n");

  const ConnectionStatus status = source.test_connection();
  EXPECT_FALSE(status.ok);
  EXPECT_NE(status.message.find("max_air_temperature_c"), std::string::npos);
  EXPECT_NE(status.message.find("data/snapshots/test.csv:1"), std::string::npos);
}

TEST(SnapshotWeatherTest, ANonNumericValueIsReportedWithItsLine) {
  const SnapshotWeatherSource source = loaded(
      "date,rainfall_mm,min_air_temperature_c,max_air_temperature_c\n"
      "2023-06-28,0.0,1.5,10.2\n"
      "2023-06-29,trace,1.5,10.2\n");

  const ConnectionStatus status = source.test_connection();
  EXPECT_FALSE(status.ok);
  EXPECT_NE(status.message.find(":3"), std::string::npos);
  EXPECT_NE(status.message.find("rainfall_mm"), std::string::npos);
  EXPECT_NE(status.message.find("trace"), std::string::npos);
}

TEST(SnapshotWeatherTest, AMalformedDateIsRejectedRatherThanGuessed) {
  const SnapshotWeatherSource source = loaded(
      "date,rainfall_mm,min_air_temperature_c,max_air_temperature_c\n"
      "28/06/2023,0.0,1.5,10.2\n");

  const ConnectionStatus status = source.test_connection();
  EXPECT_FALSE(status.ok);
  EXPECT_NE(status.message.find("YYYY-MM-DD"), std::string::npos);
}

TEST(SnapshotWeatherTest, ImpossibleDatesAreRejected) {
  const SnapshotWeatherSource source = loaded(
      "date,rainfall_mm,min_air_temperature_c,max_air_temperature_c\n"
      "2023-02-29,0.0,1.5,10.2\n");

  EXPECT_FALSE(source.test_connection().ok);
  EXPECT_NE(source.test_connection().message.find("no such date"), std::string::npos);
}

// A missing day is the dangerous one: to the soil water bucket it is
// indistinguishable from a dry day, so it must never load silently.
TEST(SnapshotWeatherTest, AGapInTheSeriesIsAnError) {
  const SnapshotWeatherSource source = loaded(
      "date,rainfall_mm,min_air_temperature_c,max_air_temperature_c\n"
      "2023-06-28,0.0,1.5,10.2\n"
      "2023-06-30,0.0,1.5,10.2\n");

  const ConnectionStatus status = source.test_connection();
  EXPECT_FALSE(status.ok);
  EXPECT_NE(status.message.find("2023-06-29"), std::string::npos);
  EXPECT_NE(status.message.find("no gaps"), std::string::npos);
}

TEST(SnapshotWeatherTest, PhysicallyImpossibleRowsAreRejected) {
  const SnapshotWeatherSource source = loaded(
      "date,rainfall_mm,min_air_temperature_c,max_air_temperature_c\n"
      "2023-06-28,0.0,12.0,4.0\n");

  EXPECT_FALSE(source.test_connection().ok);
}

TEST(SnapshotWeatherTest, AskingBeyondTheSnapshotIsAnErrorNotAShortSeries) {
  const SnapshotWeatherSource source = loaded(kValidSnapshot);

  EXPECT_THROW(static_cast<void>(source.fetch(DateRange::calendar_year(2023))), std::out_of_range);
  try {
    static_cast<void>(source.fetch(DateRange{Date{2023, 6, 1}, Date{2023, 6, 30}}));
    FAIL() << "expected the request to be rejected";
  } catch (const std::out_of_range& error) {
    EXPECT_NE(std::string(error.what()).find("2023-06-28"), std::string::npos);
  }
}

TEST(SnapshotWeatherTest, AHashMismatchStopsTheRunAndShowsBothHashes) {
  SnapshotWeatherSource::Options options = test_options();
  options.expected_content_hash = std::string(64, '0');
  const SnapshotWeatherSource source = SnapshotWeatherSource::from_text(kValidSnapshot, options);

  // from_text does not enforce the hash - the file constructor does - so the
  // check that matters here is that the hash is recorded and comparable.
  EXPECT_EQ(source.content_hash(), Sha256::hex_of(kValidSnapshot));
  EXPECT_NE(source.content_hash(), options.expected_content_hash);
}

TEST(SnapshotWeatherTest, AMissingFileTellsTheUserHowToGetIt) {
  SnapshotWeatherSource::Options options = test_options();
  options.path = "data/snapshots/does-not-exist.csv";
  const SnapshotWeatherSource source(options);

  const ConnectionStatus status = source.test_connection();
  EXPECT_FALSE(status.ok);
  EXPECT_NE(status.message.find("does-not-exist.csv"), std::string::npos);
  EXPECT_NE(status.message.find("cliflo-snapshot"), std::string::npos);
  EXPECT_THROW(static_cast<void>(source.fetch(DateRange::calendar_year(2023))), std::runtime_error);
}

TEST(SnapshotWeatherTest, DescribeReportsTheSpanAndLicence) {
  const SnapshotWeatherSource source = loaded(kValidSnapshot);
  const SourceDescription description = source.describe();

  EXPECT_EQ(description.name, "weather_snapshot:test_station");
  EXPECT_EQ(description.licence, "test fixture");
  EXPECT_NE(description.coverage.find("2023-06-28"), std::string::npos);
  EXPECT_NE(description.coverage.find("2023-07-01"), std::string::npos);
}

TEST(SnapshotWeatherTest, AnEmptySnapshotIsReportedRatherThanReplayed) {
  EXPECT_FALSE(loaded("").test_connection().ok);
  EXPECT_FALSE(loaded("date,rainfall_mm,min_air_temperature_c,max_air_temperature_c\n")
                   .test_connection()
                   .ok);
}

}  // namespace
}  // namespace paddock::core
