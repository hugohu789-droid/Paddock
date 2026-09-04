// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

// Animal classes as data.
//
// M3 asks for livestock "fully driven by species TOML", which means a ewe and a
// dairy cow differ only in the numbers a file gives them. The tests that matter
// here are the ones that stop a file from quietly claiming more than it knows:
// an unverified reference weight has to say so, and a definition that
// contradicts itself has to be refused rather than run.

#include <gtest/gtest.h>

#include <string>

#include <paddock/config/ConfigError.hpp>
#include <paddock/config/SpeciesConfig.hpp>

namespace paddock::config {
namespace {

constexpr std::string_view kEwe = R"(
[species]
name = "test_ewe"

[energy]
species_factor = { value = 1.0, status = "direct", source = "tmc_animal_me" }
sex_factor = { value = 1.0, status = "direct", source = "tmc_animal_me" }
standard_reference_weight_kg = { value = 65.0, status = "verify" }
grazing_coefficient = { value = 0.0025, status = "placeholder" }
gain_energy_ceiling_mj_per_kg = { value = 20.3, status = "direct", source = "tmc_animal_me" }

[typical]
liveweight_kg = 60.0
age_days = 1200.0
)";

SpeciesDefinition parse(std::string_view text) {
  return parse_species(text, "test.toml");
}

TEST(SpeciesConfigTest, ASpeciesLoadsIntoTheParametersTheEnergyModelUses) {
  const SpeciesDefinition species = parse(kEwe);

  EXPECT_EQ(species.name, "test_ewe");
  // The class id is the name rather than a second field to keep in step.
  EXPECT_EQ(species.energy.class_id, "test_ewe");
  EXPECT_DOUBLE_EQ(species.energy.species_factor, 1.0);
  EXPECT_DOUBLE_EQ(species.energy.standard_reference_weight_kg, 65.0);
  EXPECT_TRUE(species.energy.validation_error().empty());
  EXPECT_EQ(species.standard_reference_weight.status, Provenance::Verify);
  EXPECT_FALSE(species.fully_evidenced()) << "SRW is not evidenced";
}

// The definition feeds straight into the energy model, so the round trip is
// worth asserting: a file describes an animal the rest of the model can use
// without anything in between translating it.
TEST(SpeciesConfigTest, TheDefinitionDrivesTheEnergyModelDirectly) {
  const SpeciesDefinition species = parse(kEwe);

  core::AnimalState state;
  state.liveweight_kg = species.typical_liveweight_kg;
  state.age_days = species.typical_age_days;

  core::DietQuality diet;
  diet.metabolisable_energy_mj_per_kg_dm = 10.5;
  diet.digestibility_percent = 75.0;

  const core::EnergyRequirement need =
      core::daily_energy_requirement(species.energy, state, diet, core::GrazingConditions{});

  EXPECT_GT(need.intake_kg_dm, 0.0);
  // A 60 kg ewe at maintenance on a 10.5 MJ diet: under a kilogram of dry
  // matter, which is what bare maintenance costs. Simpson (1978b) gives 0.40 MJ
  // ME per kg lwt^0.75 for sheep, so 60^0.75 * 0.40 / 10.5 is about 0.8 kg.
  EXPECT_GT(need.intake_kg_dm, 0.5);
  EXPECT_LT(need.intake_kg_dm, 1.2);
}

// A status has no default, and that is the point of the field: defaulting to
// direct would launder a guess into a published figure, and defaulting to
// placeholder would let a real citation go unrecorded.
TEST(SpeciesConfigTest, AValueWithoutAStatusIsRefused) {
  EXPECT_THROW(static_cast<void>(parse(R"(
[species]
name = "no_flag"

[energy]
species_factor = { value = 1.0, status = "direct", source = "tmc_animal_me" }
sex_factor = { value = 1.0, status = "direct" }
standard_reference_weight_kg = { value = 65.0, status = "verify" }
grazing_coefficient = { value = 0.0025, status = "placeholder" }
gain_energy_ceiling_mj_per_kg = { value = 20.3, status = "direct", source = "tmc_animal_me" }

[typical]
liveweight_kg = 60.0
age_days = 1200.0
)")),
               ConfigError);
}

// An animal heavier than the mature weight of its own breed would run: the
// energy value of gain simply saturates, so the growth rate looks plausible and
// is wrong. That is exactly the class of mistake worth refusing.
TEST(SpeciesConfigTest, AnAnimalHeavierThanItsBreedsMatureWeightIsRefused) {
  EXPECT_THROW(static_cast<void>(parse(R"(
[species]
name = "impossible"

[energy]
species_factor = { value = 1.0, status = "direct", source = "tmc_animal_me" }
sex_factor = { value = 1.0, status = "direct", source = "tmc_animal_me" }
standard_reference_weight_kg = { value = 65.0, status = "verify" }
grazing_coefficient = { value = 0.0025, status = "placeholder" }
gain_energy_ceiling_mj_per_kg = { value = 20.3, status = "direct", source = "tmc_animal_me" }

[typical]
liveweight_kg = 90.0
age_days = 1200.0
)")),
               ConfigError);
}

TEST(SpeciesConfigTest, AMistypedKeyIsRejectedRatherThanIgnored) {
  EXPECT_THROW(static_cast<void>(parse(R"(
[species]
name = "typo"

[energy]
species_factor = { value = 1.0, status = "direct", source = "tmc_animal_me" }
sex_facter = { value = 1.0, status = "direct", source = "tmc_animal_me" }
standard_reference_weight_kg = { value = 65.0, status = "verify" }
grazing_coefficient = { value = 0.0025, status = "placeholder" }
gain_energy_ceiling_mj_per_kg = { value = 20.3, status = "direct", source = "tmc_animal_me" }

[typical]
liveweight_kg = 60.0
age_days = 1200.0
)")),
               ConfigError);
}

TEST(SpeciesConfigTest, ParametersTheEnergyModelWouldRejectAreRefusedHere) {
  EXPECT_THROW(static_cast<void>(parse(R"(
[species]
name = "no_species_factor"

[energy]
species_factor = { value = 0.0, status = "direct", source = "tmc_animal_me" }
sex_factor = { value = 1.0, status = "direct", source = "tmc_animal_me" }
standard_reference_weight_kg = { value = 65.0, status = "verify" }
grazing_coefficient = { value = 0.0025, status = "placeholder" }
gain_energy_ceiling_mj_per_kg = { value = 20.3, status = "direct", source = "tmc_animal_me" }

[typical]
liveweight_kg = 60.0
age_days = 1200.0
)")),
               ConfigError);
}

// **A file written before E110 must fail, and say where** (verify.md, E110).
//
// The six `[intake]` lactation parameters gained an `appetite_` prefix because
// the old names read as claims about milk. No alias was kept: the repository is
// pre-1.0 and every species file is in-tree, so a stale name is a mistake to be
// shown rather than absorbed. The failure has to be loud in both directions -
// `reject_unknown_keys` refuses the old key, and `require_table` refuses a file
// that has dropped it - because the one thing this must never do is default
// quietly to zero. That is E93, where an appetite scalar of 0.0 marked `direct`
// switched the whole intake ceiling off and nothing said a word for months.
TEST(SpeciesConfigTest, AStaleKeyNameIsRejectedWithItsLine) {
  const std::string stale = std::string(kEwe) + R"(
[intake]
normal_weight_rate = { value = 0.0157, status = "direct", source = "grazplan_animal" }
normal_weight_exponent = { value = 0.27, status = "direct", source = "grazplan_animal" }
normal_weight_blend = { value = 0.4, status = "direct", source = "grazplan_animal" }
condition_intake_limit = { value = 1.5, status = "direct", source = "grazplan_animal" }
lactation_peak_days = { value = 28.0, status = "direct", source = "grazplan_animal" }
)";

  try {
    parse(stale);
    FAIL() << "a file using the pre-E110 key name has to be refused, not read";
  } catch (const ConfigError& error) {
    EXPECT_EQ(error.path(), "test.toml");
    EXPECT_GT(error.line(), 0U) << "the message has to be able to point at the key";
    EXPECT_NE(error.detail().find("lactation_peak_days"), std::string::npos)
        << "and has to name the key it is refusing: " << error.detail();
  }
}

}  // namespace
}  // namespace paddock::config
