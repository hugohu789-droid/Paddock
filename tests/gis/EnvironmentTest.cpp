// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

// Whether the geospatial stack on this machine can do the one thing the whole
// milestone rests on: resolve New Zealand Transverse Mercator.
//
// These are deployment tests, not algorithm tests. The transform arithmetic is
// task #17 and lives elsewhere. What is asserted here is that GDAL and PROJ are
// present, recent enough, agree with themselves, and can read their own
// database - the four ways a geospatial build stops working when it moves to
// another machine.

#include <gtest/gtest.h>

#include <string>

#include <paddock/gis/Environment.hpp>

namespace paddock::gis {
namespace {

TEST(GisEnvironmentTest, GdalAndProjAreLinkedAndReportThemselves) {
  const LibraryVersions versions = library_versions();

  EXPECT_FALSE(versions.gdal_runtime.empty());
  EXPECT_FALSE(versions.gdal_compiled.empty());
  EXPECT_FALSE(versions.proj_runtime.empty());
  EXPECT_NE(versions.gdal_runtime, "unknown");
  EXPECT_NE(versions.proj_runtime, "unknown");
}

// A build compiled against one GDAL and running against another is the failure
// this catches. It link-checks clean, runs, and gets projections subtly wrong.
TEST(GisEnvironmentTest, TheLoadedGdalIsTheOneWeCompiledAgainst) {
  const LibraryVersions versions = library_versions();

  EXPECT_EQ(versions.gdal_runtime, versions.gdal_compiled)
      << "compiled against GDAL " << versions.gdal_compiled << " but loaded "
      << versions.gdal_runtime;
}

// PROJ 6 is the floor: it introduced proj.db and the API this module uses.
// Anything older cannot answer for EPSG:2193 the way the rest of M3 assumes.
TEST(GisEnvironmentTest, ProjIsAtLeastVersionSix) {
  const LibraryVersions versions = library_versions();

  EXPECT_GE(versions.proj_compiled_major, 6)
      << "PROJ " << versions.proj_compiled_major << "." << versions.proj_compiled_minor << "."
      << versions.proj_compiled_patch;
}

// The one that actually breaks in the field. PROJ links, runs, and fails every
// transform when it cannot find proj.db, so the failure message carries the
// search path - the thing a person needs in order to fix it.
TEST(GisEnvironmentTest, ProjCanResolveNztm2000) {
  EXPECT_TRUE(nztm_definition_available())
      << "PROJ cannot resolve EPSG:2193. Its database search path is \""
      << projection_database_search_path()
      << "\"; proj.db is missing from it, or PROJ_DATA points elsewhere.";
}

// GDAL's driver set is a build-time choice, and a build missing one still links
// and runs: the failure arrives when a file is opened, as a null dataset and a
// complaint about an unrecognised format. CLAUDE.md fixes the formats - GeoTIFF
// for rasters, GeoPackage for vectors - so this asserts them once here rather
// than leaving each reader to discover it.
TEST(GisEnvironmentTest, TheFormatsThisProjectUsesAreCompiledIn) {
  for (const std::string& driver : required_gdal_drivers()) {
    EXPECT_TRUE(gdal_driver_available(driver))
        << "GDAL was built without the " << driver
        << " driver; every read of that format will fail at the point of use";
  }
}

// The negative control for the check above: a name GDAL has never had must come
// back false, or the probe is answering yes to everything.
TEST(GisEnvironmentTest, ADriverThatDoesNotExistIsReportedMissing) {
  EXPECT_FALSE(gdal_driver_available("NotARealGdalDriver"));
}

}  // namespace
}  // namespace paddock::gis
