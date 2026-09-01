// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#pragma once

#include <cstdint>
#include <functional>
#include <string>

/// Fetching the ground a scenario names, so that measured terrain is a button
/// rather than an installation step.
///
/// **The problem this solves is not "how do we find a DEM".** It is that
/// snapshots are never shipped - they are bulk, they go stale, and the drop
/// zone they live in is shared with sources whose licences forbid
/// redistribution outright - so every download of this simulator arrives
/// without ground, and every shipped scenario draws flat. The instruction to
/// fix that was "run this Python script", which asks a farm consultant to
/// install a language runtime before they can see a hill.
///
/// **Searching and fetching are different jobs, and only one of them is hard.**
/// Finding which of thousands of 1 m tiles covers a new farm means walking a
/// STAC catalogue, checking each tile's own footprint rather than its
/// collection's box, and several hundred requests to do it. That stays in
/// scripts/nz-elevation-snapshot.py, because it is something you do once when
/// you define a farm. A *shipped* scenario has already had it done: the bundle
/// pins the tile's URL and the hash it must have, and fetching it is one GET
/// against a known address, checked against a hash decided before the request
/// was made.
///
/// **No new dependency.** This goes through GDAL's virtual file system, which
/// gis/ already links and which already speaks HTTP - the same layer that opens
/// the tile once it is here. A separate HTTP client would have been a second
/// way of reaching the network in a module that already had one.
///
/// Only LINZ elevation is fetched this way, and it is the one source where that
/// is uncomplicated: it is published as open data on S3 under CC BY 4.0 with no
/// account and no key. The cadastre needs a key of the user's own; NIWA CliFlo
/// and Manaaki Whenua S-map may not be redistributed at all and have to be
/// fetched by the person licensed for them. See docs/validation/verify.md
/// item 7.
namespace paddock::gis {

/// What to fetch, where to put it, and what it has to be.
struct ElevationDownload {
  /// Http or https. Checked again here rather than trusted, because the string
  /// arrives from a file and GDAL's virtual file system opens far more than
  /// the web.
  std::string url;

  /// Where the file lands. Written to a sibling `.partial` first and moved into
  /// place only once it is whole and checked, so an interrupted fetch never
  /// leaves something that looks like a snapshot where a scenario expects one.
  std::string destination;

  /// What the finished file must hash to. **Required, and the reason this is
  /// safe at all**: the bundle decided which file it means before any request
  /// went out, so what comes back is either that file or it is discarded.
  std::string expected_sha256;
};

/// How it went, in terms a person can be shown.
struct DownloadOutcome {
  bool ok = false;

  /// Empty when ok. Otherwise why not, written to be put in front of somebody.
  std::string reason;

  /// What arrived actually hashed to. Set even on a mismatch, because "it
  /// downloaded and it is the wrong file" and "it did not download" are
  /// different problems and only one of them is worth retrying.
  std::string sha256;

  std::int64_t bytes = 0;
};

/// Reports bytes so far and total, and returns false to cancel. Total is zero
/// when the server did not say - which S3 does say, but a cancel that only
/// works when the length is known would be a cancel that fails on the slow
/// connections that need it.
using DownloadProgress = std::function<bool(std::int64_t so_far, std::int64_t total)>;

/// Fetches, hashes, and only then puts it where the scenario is looking.
///
/// Never throws: a fetch fails for reasons that are about somebody's network
/// rather than about this program being wrong, and every one of them belongs in
/// a sentence on screen rather than in a stack unwind.
[[nodiscard]] DownloadOutcome download_elevation(const ElevationDownload& request,
                                                 const DownloadProgress& progress = {});

}  // namespace paddock::gis
