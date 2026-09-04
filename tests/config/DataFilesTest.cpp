// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

// The definitions committed under data/ have to keep loading.
//
// A configuration format is only an interface if the files that ship with the
// project are held to it. Without this suite a schema change would break every
// example silently, and the first person to notice would be someone trying the
// simulator for the first time.

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <paddock/core/AnimalEnergy.hpp>

#include <paddock/config/DiseaseConfig.hpp>
#include <paddock/config/FarmConfig.hpp>
#include <paddock/config/PastureConfig.hpp>
#include <paddock/config/ScenarioConfig.hpp>
#include <paddock/config/SoilConfig.hpp>
#include <paddock/config/SpeciesConfig.hpp>
#include <paddock/config/WeatherConfig.hpp>

namespace paddock::config {
namespace {

std::string data_path(const std::string& relative) {
  return std::string(PADDOCK_DATA_DIR) + "/" + relative;
}

TEST(DataFilesTest, TheExampleSoilLoads) {
  const SoilDefinition soil = load_soil(data_path("soils/templeton-silt-loam-example.toml"));

  EXPECT_EQ(soil.name, "templeton_silt_loam_example");
  // FAO-56 Eq. 82 on the measurements in the file.
  EXPECT_DOUBLE_EQ(soil.water.total_available_water_mm, 120.0);
  EXPECT_TRUE(soil.water.validation_error().empty());
}

TEST(DataFilesTest, TheExampleSwardLoads) {
  const core::SwardParameters sward =
      load_sward(data_path("pastures/ryegrass-clover-example.toml"));

  EXPECT_EQ(sward.grass.species_id, "ryegrass_perennial");
  EXPECT_EQ(sward.legume.species_id, "clover_white");
  EXPECT_GT(sward.legume.nitrogen_fixation_kg_per_t_dm, 0.0);
  EXPECT_TRUE(sward.validation_error().empty());
}

TEST(DataFilesTest, TheExampleWeatherSiteLoads) {
  const core::SyntheticWeatherParameters site =
      load_synthetic_weather(data_path("weather/canterbury-plains-example.toml"));

  EXPECT_EQ(site.site_name, "canterbury_plains_example");
  EXPECT_LT(site.latitude_degrees, 0.0) << "a New Zealand site is south of the equator";
  EXPECT_TRUE(site.validation_error().empty());
  // The shape of a Southern Hemisphere year: January warmer than July.
  EXPECT_GT(site.months[0].mean_daily_max_c, site.months[6].mean_daily_max_c);
}

// The farm set is discovered rather than enumerated. This test deliberately
// does NOT name the three farms that ship today: asserting a count or a list
// here would reintroduce exactly the fixed set the directory is meant to
// replace, and adding a farm would then require editing a test.
TEST(DataFilesTest, EveryFarmDescriptionLoads) {
  const std::vector<FarmDefinition> farms = load_farms(data_path("farms"));

  ASSERT_FALSE(farms.empty()) << "data/farms/ has no farm descriptions";
  for (const FarmDefinition& farm : farms) {
    EXPECT_TRUE(farm.validation_error().empty()) << farm.name << ": " << farm.validation_error();
    EXPECT_FALSE(farm.region.empty()) << farm.name;
    EXPECT_LT(farm.location.latitude_degrees, 0.0) << farm.name << " is not in New Zealand";
    // A farm whose location is not surveyed has to say what its coordinates
    // actually are. The flag is only useful if the unset case is explained.
    if (!farm.location.location_verified) {
      EXPECT_FALSE(farm.location.source.empty())
          << farm.name << " has an unverified location and no note saying what it is";
    }
  }
}

TEST(DataFilesTest, FarmNamesAreUnique) {
  const std::vector<FarmDefinition> farms = load_farms(data_path("farms"));

  std::vector<std::string> names;
  names.reserve(farms.size());
  for (const FarmDefinition& farm : farms) {
    names.push_back(farm.name);
  }
  const std::vector<std::string> sorted = names;
  EXPECT_TRUE(std::is_sorted(sorted.begin(), sorted.end()))
      << "load_farms should return farms in a stable order";
  EXPECT_EQ(std::unique(names.begin(), names.end()), names.end());
}

// Massey publishes Dairy 4's effective area and its paddock count, so this one
// farm can be checked against its source rather than against itself. The
// generated outline is a rectangle and is not claimed to be the farm's shape;
// what is claimed is that the area and the paddock size come from Massey.
//
// Validation, not a regression pin: the figures are on Massey's farm page,
// quoted in data/farms/massey-dairy-4.toml.
TEST(DataFilesTest, MasseyDairy4MatchesItsPublishedAreaAndPaddockCount) {
  const FarmDefinition farm = load_farm(data_path("farms/massey-dairy-4.toml"));

  ASSERT_TRUE(farm.stated_effective_hectares.has_value());
  // value_or rather than a dereference: gtest's ASSERT_TRUE stops the test on
  // failure, but clang-tidy's optional analysis does not model that, and a
  // check silenced with a comment is worth less than one written so it passes.
  const double stated_hectares = farm.stated_effective_hectares.value_or(0.0);
  EXPECT_DOUBLE_EQ(stated_hectares, 221.0);

  // The declared extent has to reproduce the published area, or the file is
  // describing a different farm from the one it cites.
  EXPECT_NEAR(farm.boundary_hectares(), stated_hectares, 1e-6);

  // "approximately 80 x 1.5-3.5 hectare paddocks all with race access".
  //
  // The window is 70 to 90 rather than exactly 80 because "approximately" is
  // Massey's word, not a hedge added here: the generator tiles whole paddocks
  // into a rectangle and lands where the arithmetic puts it. On this extent it
  // lands on 80, and a change that moved it outside the window would mean this
  // farm no longer reproduces the subdivision it cites.
  const std::vector<core::Paddock> paddocks = farm.make_paddocks();
  EXPECT_GE(paddocks.size(), 70U) << "generated " << paddocks.size();
  EXPECT_LE(paddocks.size(), 90U) << "generated " << paddocks.size();

  for (const core::Paddock& paddock : paddocks) {
    EXPECT_GE(paddock.area_hectares(), 1.5) << paddock.name;
    EXPECT_LE(paddock.area_hectares(), 3.5) << paddock.name;
  }

  // The mean follows from the two published figures - 221 / 80 = 2.7625 ha -
  // so it is a check on the pair rather than a third assertion.
  const double mean_hectares = farm.boundary_hectares() / static_cast<double>(paddocks.size());
  EXPECT_NEAR(mean_hectares, 221.0 / 80.0, 0.05) << "mean paddock " << mean_hectares << " ha";

  GTEST_LOG_(INFO) << paddocks.size() << " paddocks averaging " << mean_hectares << " ha, over "
                   << farm.boundary_hectares() << " ha";
}

// Like the farms, the species set is discovered rather than enumerated, and
// this test deliberately does not name the three that ship today.
TEST(DataFilesTest, EverySpeciesDefinitionLoads) {
  const std::vector<SpeciesDefinition> species = load_species_directory(data_path("species"));

  ASSERT_FALSE(species.empty()) << "data/species/ has no definitions";
  for (const SpeciesDefinition& definition : species) {
    EXPECT_TRUE(definition.validation_error().empty())
        << definition.name << ": " << definition.validation_error();
    EXPECT_TRUE(definition.energy.validation_error().empty()) << definition.name;
    EXPECT_GT(definition.typical_liveweight_kg, 0.0) << definition.name;
  }
}

// Nothing shipped may claim more evidence than it has, and the report says
// exactly what each definition is resting on.
//
// This is the test that would fail if somebody upgraded a status without adding
// a citation - which is the only way a guess gets quoted as a finding.
TEST(DataFilesTest, EveryShippedNumberDeclaresWhereItCameFrom) {
  const std::vector<SpeciesDefinition> species = load_species_directory(data_path("species"));
  const std::vector<std::string> known_sources =
      load_source_ids(data_path("calibration/livestock/sources.toml"));

  ASSERT_FALSE(known_sources.empty());

  for (const SpeciesDefinition& definition : species) {
    for (const SourcedValue* value : definition.sourced_values()) {
      // A citation has to point at a source that exists. A dangling id is
      // worse than no id: it reads as evidence and is not.
      if (!value->source_id.empty()) {
        EXPECT_NE(std::find(known_sources.begin(), known_sources.end(), value->source_id),
                  known_sources.end())
            << definition.name << " cites '" << value->source_id
            << "', which is not in sources.toml";
      }
      // And anything claiming to rest on evidence has to name it.
      EXPECT_TRUE(value->validation_error(definition.name).empty())
          << value->validation_error(definition.name);
    }

    GTEST_LOG_(INFO) << definition.name << ": weakest status is "
                     << to_string(definition.weakest_status());
  }
}

// **Standard reference weight: resolved for sheep, still open for the rest.**
//
// This test used to assert it was unresolved everywhere, and said in its own
// comment that finding a cited breed mature weight should update it rather than
// delete it, because it records a state of the evidence and not a rule. That
// happened: OVERSEER's Characteristics of animals chapter, Table 8, gives
// Romney ewes 66 kg, so the ewe's 65 kg "verify" became a sourced 66.
//
// The cattle and deer definitions are still unevidenced, and while that is true
// no absolute liveweight gain for those classes is quotable - only comparisons
// that hold the weight fixed.
TEST(DataFilesTest, StandardReferenceWeightIsEvidencedOnlyWhereItHasBeenFound) {
  const std::vector<SpeciesDefinition> species = load_species_directory(data_path("species"));

  bool saw_the_ewe = false;
  for (const SpeciesDefinition& definition : species) {
    if (definition.name == "sheep_ewe" || definition.name == "sheep_lamb") {
      saw_the_ewe = saw_the_ewe || definition.name == "sheep_ewe";
      // The lamb carries the same figure, because SRW is a mature weight: it
      // is what a lamb will grow into, not what it weighs now.
      EXPECT_TRUE(definition.standard_reference_weight.is_evidence())
          << "the sheep reference weight is Table 8 of the OVERSEER characteristics chapter";
      EXPECT_DOUBLE_EQ(definition.standard_reference_weight.value, 66.0);
      continue;
    }

    EXPECT_FALSE(definition.standard_reference_weight.is_evidence())
        << definition.name
        << " now claims an evidenced reference weight; if that is right, cite it in "
           "sources.toml and docs/validation/verify.md and update this test to match";
  }
  EXPECT_TRUE(saw_the_ewe) << "data/species/ no longer has the ewe this test is about";
}

// **What a ewe carries and milks is data, like everything else about her.** A
// class with no [reproduction] table does not breed, which is how the same
// energy model feeds a wether, a store lamb and a ewe in her last week.
TEST(DataFilesTest, TheEweBreedsAndSaysWhatItCostsFromData) {
  const SpeciesDefinition ewe = load_species(data_path("species/sheep-ewe.toml"));

  // TMC Characteristics of animals, Table 28, from Freer et al. (2006).
  EXPECT_DOUBLE_EQ(ewe.energy.gestation_length_days, 150.0);
  EXPECT_TRUE(ewe.gestation_length_days.is_evidence());

  // Milk composition is the weak link and is marked as such: published sheep
  // milk fat runs from 4.6% to 12.6% across breeds.
  EXPECT_GT(ewe.energy.milk_fat_percent, 0.0);
  EXPECT_GT(ewe.energy.milk_protein_percent, 0.0);
  EXPECT_FALSE(ewe.milk_fat_percent.is_evidence())
      << "no New Zealand Romney milk fat figure has been read; see verify.md E23";
}

// **A dairy cow in this model is still a dry cow**, and the reason changed:
// lactation is implemented now, but for sheep. No cattle reproduction data has
// been written, so cattle-dry-cow.toml has no [reproduction] table and the
// energy model charges it nothing for milk - correctly, for a dry cow. The
// definition is named for what it models, and this test keeps that true if
// someone later adds a milking class without the data behind it.
TEST(DataFilesTest, TheDairyDefinitionIsNamedForTheDryCowItActuallyModels) {
  const SpeciesDefinition cow = load_species(data_path("species/cattle-dry-cow.toml"));

  EXPECT_EQ(cow.name, "cattle_dairy_dry");
  EXPECT_NE(cow.description.find("not modelled"), std::string::npos)
      << "the description has to say lactation is absent: " << cow.description;
  // CSIRO (2007) for dairy, which is the value the OVERSEER manual follows.
  EXPECT_DOUBLE_EQ(cow.energy.species_factor, 1.4);
}

// Diseases are discovered rather than enumerated, like the farms and the
// species: naming the files here would mean adding one is a code change, which
// is what the data-not-classes rule exists to prevent.
TEST(DataFilesTest, EveryShippedDiseaseLoadsAndIsUsable) {
  const std::vector<DiseaseDefinition> diseases = load_disease_directory(data_path("diseases"));

  ASSERT_FALSE(diseases.empty()) << "data/diseases/ has no definitions";
  for (const DiseaseDefinition& disease : diseases) {
    EXPECT_FALSE(disease.name.empty());
    EXPECT_FALSE(disease.affects.empty()) << disease.name << " says nothing about what it affects";
    EXPECT_EQ(disease.mycotoxin.invalid_reason(), "") << disease.name;
    EXPECT_FALSE(disease.provenance.empty())
        << disease.name << " records no provenance, so nothing in it can be quoted";
  }
}

// **The test that stops the file and the code drifting apart again.**
//
// The sporulation rates lived in two places for a few hours - the data file and
// a hand-written struct in the unit suite - and by the time anyone looked they
// disagreed, while every test passed. This asserts the model built from the
// shipped file behaves the way the shipped file says, so a change to one that
// is not made to the other fails here.
TEST(DataFilesTest, FacialEczemaBehavesTheWayItsFileDescribes) {
  const DiseaseDefinition fe = load_disease(data_path("diseases/facial-eczema.toml"));
  const core::MycotoxinParameters& p = fe.mycotoxin;

  EXPECT_EQ(fe.name, "facial_eczema");

  // A night at the threshold with the rain the file asks for is favourable; a
  // fraction under it is not. If the file's numbers move, these move with them.
  core::DailyWeather night;
  night.date = core::Date{2024, 2, 1};
  night.min_air_temperature_c = p.grass_minimum_temperature_c;
  EXPECT_TRUE(core::night_favours_sporulation(night, p.rainfall_mm_per_48h, p));

  night.min_air_temperature_c = p.grass_minimum_temperature_c - 0.1;
  EXPECT_FALSE(core::night_favours_sporulation(night, p.rainfall_mm_per_48h, p));

  // The count rises only after a full run, and never falls below background.
  EXPECT_GT(core::next_spore_count(10000.0, p.consecutive_nights, p), 10000.0);
  EXPECT_LT(core::next_spore_count(10000.0, p.consecutive_nights - 1, p), 10000.0);
  EXPECT_DOUBLE_EQ(core::next_spore_count(0.0, 0, p), p.background_spores_per_g);

  // Every number the file marks fitted has to name what it was fitted to, and
  // every citation has to resolve - load_disease enforces the second, so this
  // checks the first is actually being used rather than quietly absent.
  int fitted = 0;
  for (const auto& [key, sourced] : fe.provenance) {
    if (sourced.status == Provenance::Fitted) {
      ++fitted;
      EXPECT_FALSE(sourced.source_id.empty()) << key << " is fitted and names nothing";
    }
  }
  EXPECT_GT(fitted, 0) << "this file is expected to carry fitted values and say so";
}

// **Clean pasture must not damage a liver, however long the run.**
//
// The count never falls below its background, so a running total of exposure
// rises every day whether or not anything happened. Three simulated years of
// Canterbury pasture that never sporulated once used to put a mob over the
// reactor threshold. This is the assertion that would catch it coming back.
TEST(DataFilesTest, YearsOfCleanPastureNeverReachTheReactorThreshold) {
  const core::MycotoxinParameters p =
      load_disease(data_path("diseases/facial-eczema.toml")).mycotoxin;

  double carried = 0.0;
  for (int day = 0; day < 365 * 5; ++day) {
    carried = core::next_exposure(carried, toxin_ng_per_g(p.background_spores_per_g, p), p);
  }

  const double ggt = core::ggt_from_exposure(carried, p);
  EXPECT_LT(ggt, p.reactor_ggt_iu_per_l)
      << "five years of pasture that never sporulated should leave a liver alone; GGT " << ggt;
  EXPECT_DOUBLE_EQ(core::liver_injury_score(ggt, p), 0.0);
}

// **The point of measuring the dose in toxin rather than spores.**
//
// Fitzgerald, Collin and Towers (1998) measured the same spore count carrying
// 2.7 times the toxin depending which strains were on the pasture - 1.41 pg per
// spore in untreated plots against 0.52 where atoxigenic strains had been
// seeded. While exposure accumulated spore-days that measurement could not
// change a result. It has to now, or the axis was moved for nothing.
TEST(DataFilesTest, AnAtoxigenicPastureDamagesLessThanAToxigenicOne) {
  const core::MycotoxinParameters toxigenic =
      load_disease(data_path("diseases/facial-eczema.toml")).mycotoxin;

  core::MycotoxinParameters atoxigenic = toxigenic;
  atoxigenic.picograms_per_spore = 0.52;

  // The same spore count on both, for a season.
  constexpr double kCount = 100'000.0;
  double carried_toxigenic = 0.0;
  double carried_atoxigenic = 0.0;
  for (int day = 0; day < 60; ++day) {
    carried_toxigenic =
        core::next_exposure(carried_toxigenic, core::toxin_ng_per_g(kCount, toxigenic), toxigenic);
    carried_atoxigenic = core::next_exposure(carried_atoxigenic,
                                             core::toxin_ng_per_g(kCount, atoxigenic), atoxigenic);
  }

  const double ggt_toxigenic = core::ggt_from_exposure(carried_toxigenic, toxigenic);
  const double ggt_atoxigenic = core::ggt_from_exposure(carried_atoxigenic, atoxigenic);

  EXPECT_GT(ggt_toxigenic, ggt_atoxigenic)
      << "the same count of less toxic spores must do less damage, or the toxin conversion is "
         "still reaching nothing";

  // And by about the ratio the paper measured, not by some other amount.
  EXPECT_NEAR(ggt_toxigenic / ggt_atoxigenic, 1.41 / 0.52, 0.15);
}

}  // namespace

// **Every directory under data/ is either shipped or deliberately left out.**
//
// This exists because the packaging script silently shipped three releases
// short. It carried an allow-list - `scenarios species pastures soils weather
// calibration farms`, every directory data/ held the day it was written - so
// data/diseases went missing from every release from M4 onward, and
// data/economics and data/regulations from the day they were added. A released
// `paddock disease` had nothing to read, and nothing said so: the archive built
// cleanly, the tests passed, and the gap only showed on a machine that had
// unpacked it.
//
// The script is a deny-list now, and this is the other half: a directory that
// should ship must not appear in the exclusions, and the exclusions must still
// be there to read. Neither is a decision anybody can forget to make.
TEST(DataFilesTest, EveryDataDirectoryIsShippedOrExplicitlyExcluded) {
  namespace fs = std::filesystem;

  // The script sits beside data/, so the data directory locates it and no
  // second build definition has to be kept in step with the first. Taken from
  // the macro rather than from data_path(""), whose trailing slash would leave
  // parent_path() pointing back at data/ itself.
  const fs::path data = fs::path(std::string(PADDOCK_DATA_DIR));
  const fs::path root = data.parent_path();
  const fs::path script = root / "scripts" / "package-release.sh";
  ASSERT_TRUE(fs::exists(script)) << script.string();

  std::ifstream in(script);
  const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  ASSERT_FALSE(text.empty());

  const std::size_t at = text.find("data_excluded=\"");
  ASSERT_NE(at, std::string::npos)
      << "the packaging script no longer names what it excludes; if it went back to an "
         "allow-list, this test cannot tell a new directory from a forgotten one";
  const std::size_t open = text.find('"', at);
  const std::size_t close = text.find('"', open + 1);
  ASSERT_NE(close, std::string::npos);
  const std::string excluded = text.substr(open + 1, close - open - 1);

  // **Fetched ground stays out**, for the licence reason the script gives.
  EXPECT_NE(excluded.find("snapshots"), std::string::npos)
      << "fetched LiDAR and cadastre must not leave this machine in an archive";

  // **Everything else ships.** Named one by one rather than iterated, because a
  // loop over what happens to be in data/ today would pass on the day somebody
  // adds an exclusion and never notices - which is the failure this is for.
  for (const char* shipped : {"scenarios", "species", "pastures", "soils", "weather", "calibration",
                              "farms", "diseases", "economics", "regulations"}) {
    EXPECT_TRUE(fs::is_directory(data / shipped)) << shipped << " is gone from data/ entirely";
    EXPECT_EQ(excluded.find(shipped), std::string::npos)
        << shipped
        << " is excluded from releases. diseases, economics and regulations were "
           "each missing for months and a released paddock could not read them";
  }

  // And a directory in data/ that this test has never heard of is a directory
  // nobody has decided about.
  const std::vector<std::string> known = {"scenarios", "species",     "pastures", "soils",
                                          "weather",   "calibration", "farms",    "diseases",
                                          "economics", "regulations", "snapshots"};
  for (const fs::directory_entry& entry : fs::directory_iterator(data)) {
    if (!entry.is_directory()) {
      continue;
    }
    const std::string name = entry.path().filename().string();
    EXPECT_NE(std::find(known.begin(), known.end(), name), known.end())
        << "data/" << name
        << " is new. Add it to this test once you have decided whether a "
           "release should carry it, and to the script's exclusions if it should not";
  }
}

// **A farm is measured against the rule it names, and against nothing if it
// names none.**
//
// New Zealand has no national nitrogen loss limit. Regional councils set them
// catchment by catchment, and the Canterbury file this repository carries says
// so itself: two farms a valley apart can sit under different ones. So reaching
// for a rule asserts which zone a farm is in.
//
// The command line always refused to. `paddock nitrogen` takes the rule as an
// argument and will not default one. The window did the opposite - it loaded
// `regulations/canterbury-nitrogen.toml` for whatever bundle was open, so
// ruakura-fe, a Waikato block, was reported against the Canterbury Land and
// Water Regional Plan.
//
// This holds the fix from both sides: the Canterbury bundles name a Canterbury
// rule, and the Waikato one names nothing at all.
TEST(DataFilesTest, AFarmIsMeasuredAgainstTheRuleItNamesAndNoOther) {
  for (const char* name : {"lincoln-lurdf", "canterbury-grazed"}) {
    const ScenarioBundle bundle =
        load_scenario(std::string(PADDOCK_DATA_DIR) + "/scenarios/" + name);
    EXPECT_FALSE(bundle.regulation_path.empty())
        << name << " is a Canterbury farm and should name the rule it is read against";
    EXPECT_NE(bundle.regulation_path.find("canterbury"), std::string::npos) << name;
    EXPECT_FALSE(bundle.economics_path.empty())
        << name << " should name the price book its costs come from";
  }

  // **The one that matters.** Ruakura is in the Waikato; this repository has no
  // Waikato rule and no Waikato cost survey, so the honest output is fewer
  // panels rather than a confident number from the wrong island.
  const ScenarioBundle waikato =
      load_scenario(std::string(PADDOCK_DATA_DIR) + "/scenarios/ruakura-fe");
  EXPECT_TRUE(waikato.regulation_path.empty())
      << "a Waikato farm must not be measured against the only rule that happens to be on disk, "
         "which is Canterbury's";
  EXPECT_TRUE(waikato.economics_path.empty())
      << "and must not be priced from a South Island survey for the same reason";
}

// **Characterisation, not validation** (verify.md, E109 and E110).
//
// This records what the shipped ewe currently does, so that a change to either
// curve is deliberate. It is deliberately not a tolerance band against measured
// data, because the measurement disagrees with the model and saying so is the
// point: Beef + Lamb New Zealand Fact Sheet 94 dates ewe milk at two to four
// weeks and ewe appetite at eight, a gap of four to six weeks. This model's gap
// is two. The number below is the model's, and it is wrong against the best New
// Zealand evidence available - it is pinned rather than fixed because fixing it
// means moving a `direct` GrazPlan constant on the strength of a farm fact
// sheet, which is open item 17 and not a licence this test grants.
TEST(DataFilesTest, TheShippedEwesTwoPeaksAreFourteenDaysApart) {
  const std::vector<SpeciesDefinition> species = load_species_directory(data_path("species"));
  const auto ewe = std::find_if(
      species.begin(), species.end(),
      [](const SpeciesDefinition& definition) { return definition.name == "sheep_ewe"; });
  ASSERT_NE(ewe, species.end()) << "data/species/ no longer has the ewe this test is about";

  core::GrazingConditions ground;
  ground.pasture_mass_t_dm_per_ha = 2.5;

  const auto state_on = [](int day) {
    core::AnimalState state;
    state.liveweight_kg = 55.0;
    state.age_days = 1500.0;
    state.days_lactating = day;
    state.young = 1.0;
    return state;
  };

  int milk_peak = 1;
  int appetite_peak = 1;
  double best_milk = 0.0;
  double best_appetite = 0.0;
  for (int day = 1; day <= 200; ++day) {
    const double milk = core::daily_milk_yield_kg(ewe->energy, state_on(day), ground);
    if (milk > best_milk) {
      best_milk = milk;
      milk_peak = day;
    }
    const double appetite = core::potential_intake_kg_dm(ewe->energy, state_on(day));
    if (appetite > best_appetite) {
      best_appetite = appetite;
      appetite_peak = day;
    }
  }

  EXPECT_EQ(milk_peak, 14) << "TMC Eq. 35, which no file configures";
  EXPECT_EQ(appetite_peak, 28) << "GrazPlan C_I8, which appetite_lactation_peak_days sets";
  EXPECT_EQ(appetite_peak - milk_peak, 14)
      << "the model's lag between milk and appetite. Beef + Lamb New Zealand puts it at four to "
         "six weeks; this is two, and open item 17 is where that is tracked";
}

}  // namespace paddock::config
