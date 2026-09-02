// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

// Scenario bundles, and the promise they make: the same bundle reproduces the
// same run, and a bundle whose inputs have moved says so instead of running.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <paddock/config/ConfigError.hpp>
#include <paddock/config/ScenarioConfig.hpp>
#include <paddock/core/Simulation.hpp>

#include "../support/ValueOf.hpp"
#include "support/BitPattern.hpp"

namespace paddock::config {
namespace {

using test_support::bit_patterns;

std::string shipped_bundle() {
  return std::string(PADDOCK_DATA_DIR) + "/scenarios/canterbury-baseline";
}

/// The bundle that carries stock, a calendar and a policy.
std::string grazed_bundle() {
  return std::string(PADDOCK_DATA_DIR) + "/scenarios/canterbury-grazed";
}

/// A copy of the shipped bundle in a temporary directory, removed on
/// destruction. Tests that tamper with a bundle tamper with this, never with
/// the committed files.
class BundleCopy {
 public:
  /// Copies `source`, or the pasture-only bundle when none is named. A test
  /// that edits a manifest needs one it can break without breaking the
  /// repository's copy, and which bundle it starts from depends on what is
  /// being broken - only the grazed ones carry stock or a policy.
  explicit BundleCopy(const std::string& source = shipped_bundle()) {
    // **The shape of data/, not just the bundle.** A manifest reaches out of its
    // own directory - `../../species/sheep-ewe.toml`, and now `../../economics/`
    // and `../../regulations/` - so copying the bundle alone gives a scenario
    // whose inputs cannot be opened, and every test using it starts failing on
    // "cannot open bundle input" instead of on whatever it was written for.
    //
    // That had already cost one test and, when [economics] was added to the
    // shipped bundles, immediately cost another. So the copy is a small data
    // tree: the bundle under scenarios/, and the sibling directories it can
    // reach, at the same relative depth.
    root_ = std::filesystem::temp_directory_path() /
            ("paddock-bundle-" +
             std::to_string(std::filesystem::hash_value(std::filesystem::path(source))) + "-" +
             testing::UnitTest::GetInstance()->current_test_info()->name());
    std::filesystem::remove_all(root_);

    directory_ = root_ / "scenarios" / std::filesystem::path(source).filename();
    std::filesystem::create_directories(directory_);
    std::filesystem::copy(source, directory_, std::filesystem::copy_options::recursive);

    const std::filesystem::path data = std::filesystem::path(PADDOCK_DATA_DIR);
    for (const char* sibling : {"species", "economics", "regulations", "pastures", "soils"}) {
      const std::filesystem::path from = data / sibling;
      if (!std::filesystem::exists(from)) {
        continue;
      }
      std::error_code ignored;
      std::filesystem::copy(from, root_ / sibling, std::filesystem::copy_options::recursive,
                            ignored);
    }
  }

  BundleCopy(const BundleCopy&) = delete;
  BundleCopy& operator=(const BundleCopy&) = delete;
  BundleCopy(BundleCopy&&) = delete;
  BundleCopy& operator=(BundleCopy&&) = delete;

  ~BundleCopy() {
    std::error_code ignored;
    std::filesystem::remove_all(root_, ignored);
  }

  [[nodiscard]] std::string path() const { return directory_.string(); }

  /// Replaces `before` with `after` in one of the bundle's files.
  void edit(const std::string& file, const std::string& before, const std::string& after) const {
    const std::filesystem::path target = directory_ / file;
    std::string text;
    {
      const std::ifstream input(target, std::ios::binary);
      std::ostringstream buffer;
      buffer << input.rdbuf();
      text = buffer.str();
    }
    const std::string::size_type position = text.find(before);
    ASSERT_NE(position, std::string::npos) << "'" << before << "' not found in " << file;
    text.replace(position, before.size(), after);
    std::ofstream output(target, std::ios::binary);
    output << text;
  }

 private:
  std::filesystem::path root_;
  std::filesystem::path directory_;
};

std::vector<double> cover_series(const core::RunResult& result) {
  std::vector<double> cover;
  cover.reserve(result.daily.size());
  for (const core::DailyRecord& day : result.daily) {
    cover.push_back(day.cover_kg_dm);
  }
  return cover;
}

std::vector<double> run_bundle(const std::string& path) {
  const ScenarioBundle bundle = load_scenario(path);
  core::Farmlet farmlet = bundle.make_farmlet();
  return cover_series(core::run(farmlet, *bundle.weather, bundle.range));
}

TEST(ScenarioBundleTest, TheShippedBundleLoadsWithItsHashesIntact) {
  const ScenarioBundle bundle = load_scenario(shipped_bundle());

  EXPECT_EQ(bundle.name, "canterbury_baseline");
  EXPECT_EQ(bundle.master_seed, 20240701U);
  EXPECT_EQ(bundle.range.first, (core::Date{2023, 7, 1}));
  EXPECT_EQ(bundle.range.last, (core::Date{2024, 6, 30}));
  EXPECT_DOUBLE_EQ(bundle.latitude_degrees, -43.5);
  EXPECT_EQ(bundle.inputs.size(), 3U);
  EXPECT_TRUE(bundle.inputs_unchanged());
  for (const BundleInput& input : bundle.inputs) {
    EXPECT_EQ(input.recorded_sha256.size(), 64U) << input.relative_path;
    EXPECT_EQ(input.recorded_sha256, input.actual_sha256) << input.relative_path;
  }
}

// A manifest may say its ground is a file, and this is the test that says the
// section is reachable at all.
//
// It was not. `[terrain]` was parsed and never added to the manifest's list of
// permitted top-level keys, so every bundle carrying one was refused with
// "unknown key 'terrain'" - and nothing caught it, because the tests that
// exercised terrain all set it on a bundle in memory and never parsed one from
// a file. A feature reachable only from a test is a feature nobody has.
// A bundle can carry the rules its farmer works to.
//
// Until it could, a managed run put the calendar in the manifest and the
// farmer's judgement in whatever code started it, so the result could only be
// reproduced by somebody who also had that code. A bundle is supposed to be the
// whole of a run.
// **A bundle says where its fences came from, because they came from nowhere.**
// Every shipped scenario subdivides its own extent, and the window names real
// New Zealand farms - so somebody who knows one of them is told that the shape
// on screen is a demonstration rather than left to wonder why it is unfamiliar.
// The note carries the size it was cut at, and it goes empty for a bundle with
// no paddocks at all, which is what will make it disappear when boundaries come
// from a survey.
TEST(ScenarioBundleTest, ABundleSaysWhereItsFencesCameFrom) {
  const ScenarioBundle subdivided = load_scenario(grazed_bundle());
  const std::string note = subdivided.paddock_caveat();

  ASSERT_FALSE(note.empty()) << "a subdivided farm does not say that is what it is";
  EXPECT_NE(note.find("not this farm's actual paddocks"), std::string::npos);
  EXPECT_NE(note.find("2 ha"), std::string::npos)
      << "the note should carry the size the extent was cut at, read from the bundle";

  // The pasture-only bundle has no paddocks at all, so it has no fences to
  // explain - which is the same branch a bundle taking its boundaries from a
  // survey will one day take.
  const ScenarioBundle unfenced = load_scenario(shipped_bundle());
  EXPECT_TRUE(unfenced.paddock_caveat().empty());
}

TEST(ScenarioBundleTest, AGrazedBundleCarriesTheRulesItsFarmerWorksTo) {
  const ScenarioBundle bundle = load_scenario(grazed_bundle());

  if (!bundle.management.has_value()) {
    FAIL() << "a grazed bundle should carry the rules its farmer works to";
  }
  const core::ManagementPolicy& policy = *bundle.management;
  EXPECT_DOUBLE_EQ(policy.minimum_cover_kg_dm_per_ha, 1600.0);
  EXPECT_DOUBLE_EQ(policy.rotation_cover_threshold_kg_dm_per_ha, 2200.0);
  EXPECT_TRUE(policy.may_buy_feed);

  // A calendar and a policy are different things and a bundle with stock has
  // both: the calendar says which system applies when, the policy says what
  // the farmer will not allow while running it.
  EXPECT_FALSE(bundle.grazing.empty());
}

// The section is optional, because every bundle written before it existed
// leaves the policy to its caller and is still a valid bundle.
TEST(ScenarioBundleTest, ABundleWithoutStockNeedsNoPolicy) {
  const ScenarioBundle bundle = load_scenario(shipped_bundle());

  EXPECT_FALSE(bundle.management.has_value());
  EXPECT_TRUE(bundle.mobs.empty());
}

// Two rules that cannot both hold. A floor at or above the rotation threshold
// tells the farmer to start rotating only once the sward is already below the
// cover being protected, which is not strictness - it is a contradiction, and
// it runs perfectly happily while meaning nothing.
TEST(ScenarioBundleTest, ACoverFloorAboveTheRotationThresholdIsRefused) {
  const BundleCopy copy{grazed_bundle()};
  copy.edit("scenario.toml", "minimum_cover_kg_dm_per_ha = 1600.0",
            "minimum_cover_kg_dm_per_ha = 2600.0");

  try {
    static_cast<void>(load_scenario(copy.path()));
    FAIL() << "expected a cover floor above the rotation threshold to be refused";
  } catch (const ConfigError& error) {
    const std::string message = error.what();
    EXPECT_NE(message.find("rotation"), std::string::npos) << message;
  }
}

// Feed with no energy in it would be bought forever and never fill anything.
TEST(ScenarioBundleTest, SupplementWithNoEnergyInItIsRefused) {
  const BundleCopy copy{grazed_bundle()};
  copy.edit("scenario.toml", "supplement_me_mj_per_kg_dm = 10.0",
            "supplement_me_mj_per_kg_dm = 0.0");

  EXPECT_THROW(static_cast<void>(load_scenario(copy.path())), ConfigError);
}

TEST(ScenarioBundleTest, ABundleMayTakeItsGroundFromASnapshot) {
  const ScenarioBundle bundle =
      load_scenario(std::string(PADDOCK_DATA_DIR) + "/scenarios/lincoln-lurdf");

  EXPECT_EQ(bundle.terrain.kind, TerrainSpec::Kind::Snapshot);
  EXPECT_FALSE(bundle.terrain.is_flat());
  EXPECT_EQ(bundle.terrain.elevation_path, "../../snapshots/lincoln-dem-1m.tiff");
  EXPECT_EQ(bundle.terrain.elevation_sha256.size(), 64U);

  // Loading names the file and does not open it: the snapshot is tens of
  // megabytes, is not committed, and most commands never touch it.
  EXPECT_EQ(bundle.elevation, nullptr);
}

// Every shipped bundle now stands on measured ground.
//
// The demonstration bundles used to be flat, and their grid used to sit on a
// round-number coordinate that turned out to be central Christchurch. They were
// moved onto farmland west of Lincoln - chosen from the LINZ cadastre, see the
// note above [grid] - and onto the same LiDAR tile the research farm uses. One
// snapshot, one hash, three farms.
//
// What this test guards is that they all still point at the same file. Three
// bundles quietly drifting onto three copies of the same survey is the kind of
// thing nobody notices until one of them is stale.
TEST(ScenarioBundleTest, TheShippedBundlesStandOnTheSameMeasuredGround) {
  std::string shared_path;
  std::string shared_hash;
  for (const char* name : {"canterbury-baseline", "canterbury-grazed", "lincoln-lurdf"}) {
    const ScenarioBundle bundle =
        load_scenario(std::string(PADDOCK_DATA_DIR) + "/scenarios/" + name);
    EXPECT_EQ(bundle.terrain.kind, TerrainSpec::Kind::Snapshot) << name;
    EXPECT_FALSE(bundle.terrain.is_flat()) << name;
    EXPECT_EQ(bundle.terrain.elevation_sha256.size(), 64U) << name;

    // Named and not opened. The snapshot is tens of megabytes and is not
    // committed; most commands never touch it.
    EXPECT_EQ(bundle.elevation, nullptr) << name;

    if (shared_path.empty()) {
      shared_path = bundle.terrain.elevation_path;
      shared_hash = bundle.terrain.elevation_sha256;
    } else {
      EXPECT_EQ(bundle.terrain.elevation_path, shared_path) << name;
      EXPECT_EQ(bundle.terrain.elevation_sha256, shared_hash) << name;
    }
  }
}

// And that a bundle which names ground nobody can read refuses, rather than
// running flat and saying nothing. This suite has no geospatial stack, which
// makes it the right place to check the refusal actually happens.
TEST(ScenarioBundleTest, GroundThatCannotBeReadIsRefusedRatherThanIgnored) {
  const ScenarioBundle bundle =
      load_scenario(std::string(PADDOCK_DATA_DIR) + "/scenarios/canterbury-grazed");
  EXPECT_THROW((void)bundle.make_elevation(), std::runtime_error);
}

TEST(ScenarioBundleTest, TheBundleRunsAndItsBudgetsClose) {
  const ScenarioBundle bundle = load_scenario(shipped_bundle());
  core::Farmlet farmlet = bundle.make_farmlet();

  const core::RunResult result = core::run(farmlet, *bundle.weather, bundle.range);

  // 2024 is a leap year, so the farm year is 366 days long.
  EXPECT_EQ(result.daily.size(), 366U);
  EXPECT_TRUE(result.budgets_close(farmlet));
  EXPECT_GT(result.summary.total_growth_kg_dm, 0.0);
  EXPECT_GT(result.summary.total_rainfall_mm, 0.0);
  EXPECT_EQ(result.weather_provenance.source_name, "synthetic");
}

// The whole point of a bundle. Loading and running it twice gives the same
// numbers to the last bit - on this platform and this engine version, which is
// exactly what the manifest pins.
TEST(ScenarioBundleTest, RunningTheSameBundleTwiceIsBitIdentical) {
  EXPECT_EQ(bit_patterns(run_bundle(shipped_bundle())), bit_patterns(run_bundle(shipped_bundle())));
}

TEST(ScenarioBundleTest, AnotherSeedIsAnotherRun) {
  const BundleCopy copy;
  // The manifest is not itself hashed, so changing the seed leaves the input
  // hashes intact and only the run changes.
  copy.edit("scenario.toml", "master_seed = 20240701", "master_seed = 20240702");

  EXPECT_NE(bit_patterns(run_bundle(shipped_bundle())), bit_patterns(run_bundle(copy.path())));
}

// A bundle is only reproducible if its inputs are the ones it was built on.
TEST(ScenarioBundleTest, AChangedInputIsRefusedAndNamed) {
  const BundleCopy copy;
  // Valid TOML and a plausible edit: exactly the kind that would otherwise go
  // unnoticed and quietly change every result that followed.
  copy.edit("soil.toml", "runoff_fraction = 0.05", "runoff_fraction = 0.15");

  try {
    static_cast<void>(load_scenario(copy.path()));
    FAIL() << "expected the changed input to be refused";
  } catch (const ConfigError& error) {
    const std::string message = error.what();
    EXPECT_NE(message.find("soil.toml"), std::string::npos) << message;
    EXPECT_NE(message.find("recorded"), std::string::npos) << message;
    EXPECT_NE(message.find("actual"), std::string::npos) << message;
  }
}

TEST(ScenarioBundleTest, AnEngineVersionMismatchIsRefused) {
  const BundleCopy copy;
  copy.edit("scenario.toml", "engine_version = \"0.1.0\"", "engine_version = \"0.0.9\"");

  try {
    static_cast<void>(load_scenario(copy.path()));
    FAIL() << "expected the version mismatch to be refused";
  } catch (const ConfigError& error) {
    const std::string message = error.what();
    EXPECT_NE(message.find("0.0.9"), std::string::npos) << message;
    EXPECT_NE(message.find("Re-record"), std::string::npos) << message;
  }
}

TEST(ScenarioBundleTest, LoadingUncheckedIgnoresTheHashesAndTheVersion) {
  // The tool that writes the hashes has to be able to read a bundle that does
  // not have them yet; nothing else may.
  const BundleCopy copy;
  copy.edit("soil.toml", "runoff_fraction = 0.05", "runoff_fraction = 0.15");

  const ScenarioBundle bundle = load_scenario_unchecked(copy.path());

  EXPECT_EQ(bundle.name, "canterbury_baseline");
  EXPECT_FALSE(bundle.inputs_unchanged());
  EXPECT_EQ(bundle.changed_inputs().size(), 1U);
}

TEST(ScenarioBundleTest, AMissingBundleIsReportedWithItsPath) {
  try {
    static_cast<void>(load_scenario(std::string(PADDOCK_DATA_DIR) + "/scenarios/not-a-bundle"));
    FAIL() << "expected the missing bundle to be refused";
  } catch (const ConfigError& error) {
    EXPECT_NE(std::string(error.what()).find("scenario.toml"), std::string::npos);
    EXPECT_NE(std::string(error.what()).find("cannot open"), std::string::npos);
  }
}

TEST(ScenarioBundleTest, AnUnknownWeatherKindIsRefused) {
  const BundleCopy copy;
  copy.edit("scenario.toml", "kind = \"synthetic\"", "kind = \"cliflo_live\"");

  try {
    static_cast<void>(load_scenario_unchecked(copy.path()));
    FAIL() << "expected the unknown weather kind to be refused";
  } catch (const ConfigError& error) {
    EXPECT_NE(std::string(error.what()).find("cliflo_live"), std::string::npos);
    EXPECT_NE(std::string(error.what()).find("synthetic, snapshot"), std::string::npos);
  }
}

}  // namespace

// **The check that keeps one quantity from having two owners.** A bundle's
// [grid] section varies available water across the farm by REPLACING what
// soil.toml computes, not by scaling it - so for a while a soil profile stating
// 120 mm ran on a farm averaging 100, and editing the soil file changed not one
// figure in the output. Every shipped bundle now agrees with its own soil, and
// this is what says so.
TEST(ScenarioTest, EveryBundlesWaterGradientAgreesWithItsSoil) {
  for (const char* name :
       {"canterbury-baseline", "canterbury-grazed", "lincoln-lurdf", "ruakura-fe"}) {
    const ScenarioBundle bundle =
        load_scenario(std::string(PADDOCK_DATA_DIR) + "/scenarios/" + name);
    ASSERT_TRUE(bundle.grid.has_value()) << name;

    const GridSpec& grid = tests::value_of(bundle.grid, "a [grid] section");
    const double gradient_mean =
        (grid.available_water_west_mm + grid.available_water_east_mm) / 2.0;
    EXPECT_NEAR(gradient_mean, bundle.soil.total_available_water_mm,
                0.1 * bundle.soil.total_available_water_mm)
        << name << ": the grid averages " << gradient_mean
        << " mm of available water and soil.toml computes " << bundle.soil.total_available_water_mm
        << ". The grid overrides the soil, so the profile would never be used.";

    EXPECT_NO_THROW(static_cast<void>(bundle.make_soil_raster())) << name;
  }
}

// The other half: a bundle whose two figures disagree has to be refused, or the
// check above is only describing what happens to be true today.
TEST(ScenarioTest, ABundleWhoseGridDisagreesWithItsSoilIsRefused) {
  ScenarioBundle bundle =
      load_scenario(std::string(PADDOCK_DATA_DIR) + "/scenarios/canterbury-grazed");
  ASSERT_TRUE(bundle.grid.has_value());

  GridSpec& grid = tests::value_of(bundle.grid, "a [grid] section");
  grid.available_water_west_mm = 20.0;
  grid.available_water_east_mm = 40.0;  // averages 30 against a stated 120

  EXPECT_THROW(static_cast<void>(bundle.make_soil_raster()), std::runtime_error);
}

// **A bundle says where its ground is published, not only what it must be.**
//
// The hash was always enough to know *which* file a scenario means. It was
// never enough to get it: the instruction was to run a Python script, and every
// download of this simulator arrives with no ground and no runtime to fetch it
// with. A URL beside the hash turns that into one request against a known
// address, checked against a hash decided before it went out.
TEST(ScenarioTerrainTest, AShippedBundleSaysWhereItsGroundIsPublished) {
  for (const char* name :
       {"canterbury-baseline", "canterbury-grazed", "lincoln-lurdf", "ruakura-fe"}) {
    const ScenarioBundle bundle =
        load_scenario(std::string(PADDOCK_DATA_DIR) + "/scenarios/" + name);
    EXPECT_TRUE(bundle.terrain.is_fetchable()) << name;
    EXPECT_EQ(bundle.terrain.elevation_url.rfind("https://", 0), 0U) << name;
    EXPECT_FALSE(bundle.terrain.elevation_attribution.empty()) << name;

    // The licence asks for the licensor by name, and this is the string that
    // will be shown. LINZ elevation is CC BY 4.0 and the credit says so.
    EXPECT_NE(bundle.terrain.elevation_attribution.find("LINZ"), std::string::npos) << name;
    EXPECT_NE(bundle.terrain.elevation_attribution.find("CC-BY-4.0"), std::string::npos) << name;
  }
}

// **A url with no attribution is refused at load.**
//
// Not a lint. The file this fetches belongs to somebody and its licence asks to
// be credited, and the credit has to be in the archive whether or not anybody
// ever runs the download - an attribution that only exists after a successful
// network call is not an attribution. Pinning it in the manifest is what makes
// that true, and requiring it is what keeps it true.
TEST(ScenarioTerrainTest, AGroundSourceWithoutItsCreditIsRefused) {
  const BundleCopy copy;
  copy.edit("scenario.toml", "attribution = \"Canterbury", "# attribution = \"Canterbury");

  try {
    const ScenarioBundle loaded = load_scenario(copy.path());
    FAIL() << "a [terrain] naming a url and no attribution loaded, with url "
           << loaded.terrain.elevation_url;
  } catch (const std::exception& trouble) {
    const std::string said = trouble.what();
    EXPECT_NE(said.find("attribution"), std::string::npos) << said;
  }
}

// **The manifest decides what the downloader may open, and it may only open the
// web.**
//
// The URL goes to GDAL's virtual file system, which opens a great deal more
// than http - a local path, a member of an archive, another machine's share.
// A manifest that could name any of those is a manifest that can be made to
// read a file the person running it did not choose, so the string is narrowed
// where it enters the program rather than where it is used.
TEST(ScenarioTerrainTest, AGroundSourceThatIsNotAWebAddressIsRefused) {
  for (const char* scheme : {"file:///etc/passwd", "/vsizip//tmp/x.zip/y.tif", "C:/x.tif"}) {
    const BundleCopy copy;
    copy.edit("scenario.toml", "url = \"https://nz-elevation.s3-ap-southeast-2.amazonaws.com",
              std::string("url = \"") + scheme + "#");

    try {
      const ScenarioBundle loaded = load_scenario(copy.path());
      FAIL() << "a [terrain] url of '" << scheme << "' loaded as '" << loaded.terrain.elevation_url
             << "'";
    } catch (const std::exception& trouble) {
      const std::string said = trouble.what();
      EXPECT_NE(said.find("https://"), std::string::npos) << said;
    }
  }
}

// A bundle may still carry ground somebody produced themselves, with nowhere to
// fetch it from. That is a valid bundle and it simply is not fetchable.
TEST(ScenarioTerrainTest, GroundWithNoPublishedAddressIsStillValidAndSimplyNotFetchable) {
  const BundleCopy copy;
  copy.edit("scenario.toml", "url = \"https://", "# url = \"https://");
  copy.edit("scenario.toml", "attribution = \"Canterbury", "# attribution = \"Canterbury");

  const ScenarioBundle bundle = load_scenario(copy.path());
  EXPECT_EQ(bundle.terrain.kind, TerrainSpec::Kind::Snapshot);
  EXPECT_FALSE(bundle.terrain.is_fetchable());
}

}  // namespace paddock::config
