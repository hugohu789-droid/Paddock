// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#include <algorithm>
#include <cpl_vsi.h>
#include <cstdio>
#include <filesystem>
#include <system_error>
#include <vector>

#include <paddock/core/Sha256.hpp>
#include <paddock/gis/ElevationDownload.hpp>

namespace paddock::gis {

namespace {

/// A megabyte, which is what the Python script reads in and what a 1 m tile of
/// New Zealand is a few dozen of.
constexpr std::size_t kBlockBytes = 1U << 20U;

bool starts_with(const std::string& text, const char* prefix) {
  return text.rfind(prefix, 0) == 0;
}

/// Removes a file and says nothing if it was not there.
void discard(const std::filesystem::path& path) {
  std::error_code ignored;
  std::filesystem::remove(path, ignored);
}

}  // namespace

DownloadOutcome download_elevation(const ElevationDownload& request,
                                   const DownloadProgress& progress) {
  DownloadOutcome outcome;

  // **Checked here as well as where the manifest was parsed.** The config layer
  // already refuses a URL that is not http or https, and this function is
  // callable without going through it. A check that only exists at one of two
  // entrances is a check somebody will walk around.
  if (!starts_with(request.url, "https://") && !starts_with(request.url, "http://")) {
    outcome.reason =
        "the ground is fetched over the web; '" + request.url + "' is not a web address";
    return outcome;
  }
  if (request.expected_sha256.empty()) {
    // Refusing rather than fetching-and-trusting. Without a hash decided in
    // advance there is nothing to compare what arrives against, and a
    // downloader that writes whatever it is sent into the path a scenario reads
    // from is a different and much worse program than this one.
    outcome.reason =
        "no hash was given for the file to be checked against, so there is no way to "
        "know that what arrives is the ground this scenario means";
    return outcome;
  }

  const std::filesystem::path destination(request.destination);
  const std::filesystem::path partial = std::filesystem::path(request.destination + ".partial");

  std::error_code trouble;
  if (!destination.parent_path().empty()) {
    std::filesystem::create_directories(destination.parent_path(), trouble);
    if (trouble) {
      outcome.reason =
          "cannot create " + destination.parent_path().string() + ": " + trouble.message();
      return outcome;
    }
  }

  // GDAL's own HTTP layer, which is already linked and is what opens the tile
  // afterwards. VSIFOpenL on /vsicurl/ streams rather than buffering the whole
  // file, which matters: these are tens of megabytes and a farm may want
  // several.
  const std::string vsi_path = "/vsicurl/" + request.url;
  VSILFILE* remote = VSIFOpenL(vsi_path.c_str(), "rb");
  if (remote == nullptr) {
    outcome.reason = "cannot reach " + request.url +
                     ". Check the connection; the elevation service needs no account, so this is "
                     "not a permissions problem.";
    return outcome;
  }

  // What the server said it is, for the progress bar. Zero when it did not say.
  std::int64_t total = 0;
  if (VSIFSeekL(remote, 0, SEEK_END) == 0) {
    total = static_cast<std::int64_t>(VSIFTellL(remote));
    VSIFSeekL(remote, 0, SEEK_SET);
  }

  std::FILE* out = std::fopen(partial.string().c_str(), "wb");
  if (out == nullptr) {
    VSIFCloseL(remote);
    outcome.reason = "cannot write " + partial.string();
    return outcome;
  }

  core::Sha256 hash;
  std::vector<std::uint8_t> block(kBlockBytes);
  bool cancelled = false;
  bool write_failed = false;

  while (true) {
    const std::size_t got = VSIFReadL(block.data(), 1, block.size(), remote);
    if (got == 0) {
      break;
    }
    if (std::fwrite(block.data(), 1, got, out) != got) {
      write_failed = true;
      break;
    }
    hash.update(block.data(), got);
    outcome.bytes += static_cast<std::int64_t>(got);

    if (progress && !progress(outcome.bytes, total)) {
      cancelled = true;
      break;
    }
  }

  std::fclose(out);
  VSIFCloseL(remote);

  if (cancelled) {
    discard(partial);
    outcome.reason = "cancelled";
    return outcome;
  }
  if (write_failed) {
    discard(partial);
    outcome.reason = "ran out of room writing " + partial.string();
    return outcome;
  }

  outcome.sha256 = hash.hex_finish();
  if (outcome.sha256 != request.expected_sha256) {
    // **Discarded, not kept for inspection.** The partial file sits beside the
    // path a scenario reads from, and a wrong file left there under any name is
    // one rename away from being loaded as the right one.
    discard(partial);
    outcome.reason = "what arrived hashes to " + outcome.sha256 +
                     " and this scenario's ground is " + request.expected_sha256 +
                     ". Either the published tile has been revised or something between here and "
                     "the service changed it; either way it is not the ground this scenario was "
                     "written against.";
    return outcome;
  }

  // Only now does anything appear where a scenario is looking.
  std::filesystem::rename(partial, destination, trouble);
  if (trouble) {
    // rename across devices fails; a copy does not.
    std::filesystem::copy_file(partial, destination,
                               std::filesystem::copy_options::overwrite_existing, trouble);
    discard(partial);
    if (trouble) {
      outcome.reason = "downloaded and checked, but cannot put it at " + destination.string() +
                       ": " + trouble.message();
      return outcome;
    }
  }

  outcome.ok = true;
  return outcome;
}

}  // namespace paddock::gis
