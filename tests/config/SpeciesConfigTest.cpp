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
species_factor = 1.0
sex_factor = 1.0
standard_reference_weight_kg = 65.0
reference_weight_verified = false
grazing_coefficient = 0.0025
gain_energy_ceiling_mj_per_kg = 20.3

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
  EXPECT_FALSE(species.reference_weight_verified);
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

// The flag exists so a guess cannot pass as a published figure. Leaving it out
// must be an error rather than defaulting either way: defaulting to true would
// launder a guess, and defaulting to false would let a real citation go
// unrecorded.
TEST(SpeciesConfigTest, TheReferenceWeightFlagCannotBeOmitted) {
  EXPECT_THROW(static_cast<void>(parse(R"(
[species]
name = "no_flag"

[energy]
species_factor = 1.0
sex_factor = 1.0
standard_reference_weight_kg = 65.0
grazing_coefficient = 0.0025
gain_energy_ceiling_mj_per_kg = 20.3

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
species_factor = 1.0
sex_factor = 1.0
standard_reference_weight_kg = 65.0
reference_weight_verified = false
grazing_coefficient = 0.0025
gain_energy_ceiling_mj_per_kg = 20.3

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
species_factor = 1.0
sex_facter = 1.0
standard_reference_weight_kg = 65.0
reference_weight_verified = false
grazing_coefficient = 0.0025
gain_energy_ceiling_mj_per_kg = 20.3

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
species_factor = 0.0
sex_factor = 1.0
standard_reference_weight_kg = 65.0
reference_weight_verified = false
grazing_coefficient = 0.0025
gain_energy_ceiling_mj_per_kg = 20.3

[typical]
liveweight_kg = 60.0
age_days = 1200.0
)")),
               ConfigError);
}

}  // namespace
}  // namespace paddock::config
