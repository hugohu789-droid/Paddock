// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

/// The guards on fetching ground, which are the half that can be tested without
/// a network.
///
/// **Nothing here reaches LINZ, deliberately.** A test that downloads 34 MB
/// from a public bucket fails when a runner has no route out, fails when the
/// service is busy, and passes for reasons that have nothing to do with this
/// code - which is the definition of a flaky gate, and this project does not
/// keep those. What is worth asserting is what the function refuses to do
/// before any request goes out, and that is all local.
///
/// The download path itself was exercised by hand against the real service:
/// 34 MB from the Canterbury Selwyn 2023 collection, hash matching the one
/// data/scenarios/lincoln-lurdf pins.

#include <gtest/gtest.h>

#include <filesystem>
#include <string>

#include <paddock/gis/ElevationDownload.hpp>

namespace paddock::gis {
namespace {

ElevationDownload request_for(const std::string& url) {
  ElevationDownload request;
  request.url = url;
  request.destination =
      (std::filesystem::temp_directory_path() / "paddock-download-test.tiff").string();
  request.expected_sha256 = std::string(64, 'a');
  return request;
}

// **GDAL's virtual file system opens far more than the web**, and this is the
// second of the two places that is narrowed - the first being where the
// manifest is parsed. A check at one of two entrances is a check somebody walks
// around, and this function is callable without going through a manifest at
// all.
TEST(ElevationDownloadTest, RefusesAnythingThatIsNotAWebAddress) {
  for (const std::string& url : {
           std::string("file:///etc/passwd"),
           std::string("/vsizip//tmp/whatever.zip/inside.tif"),
           std::string("/vsis3/somebodys-bucket/tile.tif"),
           std::string("C:/Windows/System32/config/SAM"),
           std::string("ftp://example.invalid/tile.tif"),
           std::string(),
       }) {
    const DownloadOutcome outcome = download_elevation(request_for(url));
    EXPECT_FALSE(outcome.ok) << url;
    EXPECT_NE(outcome.reason.find("web address"), std::string::npos) << url;
    EXPECT_EQ(outcome.bytes, 0);
  }
}

// **Without a hash there is nothing to check against**, and a downloader that
// writes whatever it is handed into the path a scenario reads its ground from
// is a different and much worse program than this one. It refuses rather than
// fetching on trust.
TEST(ElevationDownloadTest, RefusesToFetchWithNothingToCheckTheResultAgainst) {
  ElevationDownload request = request_for("https://nz-elevation.example.invalid/tile.tiff");
  request.expected_sha256.clear();

  const DownloadOutcome outcome = download_elevation(request);
  EXPECT_FALSE(outcome.ok);
  EXPECT_NE(outcome.reason.find("hash"), std::string::npos) << outcome.reason;

  // And it refused before opening anything, which is the point: no partial file
  // was created for a request that was never going to be checkable.
  EXPECT_FALSE(std::filesystem::exists(request.destination + ".partial"));
}

// A refusal is a sentence, not an exception. Fetching fails for reasons about
// somebody's network rather than about this program being wrong, and every one
// of them belongs on screen.
TEST(ElevationDownloadTest, NeverThrows) {
  EXPECT_NO_THROW({
    const DownloadOutcome outcome = download_elevation(request_for("not a url at all"));
    EXPECT_FALSE(outcome.ok);
  });
}

}  // namespace
}  // namespace paddock::gis
