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

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

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

// **The species evidence aggregate covers what the file declares** (verify.md,
// E111).
//
// `sourced_values()` returned five of twenty-five: the `[energy]` table, and
// nothing from `[intake]` or `[reproduction]`. Both of those were parsed,
// validated, and checked against sources.toml one value at a time, and then
// left out of the only aggregate a species has - so a definition could carry a
// placeholder appetite coefficient and still answer `fully_evidenced()`. These
// tests fix that boundary in place from both sides: everything declared counts,
// and nothing undeclared does.

/// An [energy] table with nothing weak in it, so the tests below can put the
/// weakness where they mean to.
constexpr std::string_view kEvidencedEnergy = R"(
[species]
name = "test_ewe"

[energy]
species_factor = { value = 1.0, status = "direct", source = "tmc_animal_me" }
sex_factor = { value = 1.0, status = "direct", source = "tmc_animal_me" }
standard_reference_weight_kg = { value = 66.0, status = "direct", source = "tmc_characteristics" }
grazing_coefficient = { value = 0.0025, status = "direct", source = "tmc_animal_me" }
gain_energy_ceiling_mj_per_kg = { value = 20.3, status = "direct", source = "tmc_animal_me" }

[typical]
liveweight_kg = 60.0
age_days = 1200.0
)";

constexpr std::string_view kDirectAppetiteScalar =
    R"(appetite_scalar_per_day = { value = 0.04, status = "direct", source = "grazplan_animal" })";
constexpr std::string_view kDirectSizeCoefficient =
    R"(appetite_size_coefficient = { value = 1.7, status = "direct", source = "grazplan_animal" })";
constexpr std::string_view kDirectConditionLimit =
    R"(condition_intake_limit = { value = 1.5, status = "direct", source = "grazplan_animal" })";
constexpr std::string_view kDirectGrazingCoefficient =
    R"(grazing_coefficient = { value = 0.0025, status = "direct", source = "tmc_animal_me" })";

/// Replaces one whole parameter line, so a test can put a single weak value
/// into an otherwise fully evidenced file.
std::string weaken(std::string text, std::string_view line, std::string_view with) {
  const std::size_t at = text.find(line);
  EXPECT_NE(at, std::string::npos) << "the fixture no longer contains: " << line;
  if (at != std::string::npos) {
    text.replace(at, line.size(), with);
  }
  return text;
}

/// A whole [intake] table, every value direct.
std::string intake_table() {
  return R"(
[intake]
normal_weight_rate = { value = 0.0157, status = "direct", source = "grazplan_animal" }
normal_weight_exponent = { value = 0.27, status = "direct", source = "grazplan_animal" }
normal_weight_blend = { value = 0.4, status = "direct", source = "grazplan_animal" }
condition_intake_limit = { value = 1.5, status = "direct", source = "grazplan_animal" }
appetite_lactation_peak_days = { value = 28.0, status = "direct", source = "grazplan_animal" }
appetite_lactation_curve_exponent = { value = 1.4, status = "direct", source = "grazplan_animal" }
appetite_lactation_peak_no_young = { value = 0.524, status = "direct", source = "grazplan_animal" }
appetite_lactation_peak_one_young = { value = 0.524, status = "direct", source = "grazplan_animal" }
appetite_lactation_peak_two_young = { value = 0.707, status = "direct", source = "grazplan_animal" }
appetite_lactation_peak_three_young = { value = 0.891, status = "direct", source = "grazplan_animal" }
appetite_scalar_per_day = { value = 0.04, status = "direct", source = "grazplan_animal" }
appetite_size_coefficient = { value = 1.7, status = "direct", source = "grazplan_animal" }
availability_rate_per_kg_dm = { value = 0.00112, status = "direct", source = "grazplan_animal" }
grazing_time_increase = { value = 0.6, status = "direct", source = "grazplan_animal" }
grazing_time_rate_per_kg_dm = { value = 0.00112, status = "direct", source = "grazplan_animal" }
)";
}

std::string evidenced_species() {
  return std::string(kEvidencedEnergy) + intake_table();
}

TEST(SpeciesConfigTest, AllDirectEnergyAndAllDirectIntakeIsFullyEvidenced) {
  const SpeciesDefinition species = parse(evidenced_species());

  EXPECT_TRUE(species.declares_intake);
  EXPECT_FALSE(species.declares_reproduction) << "this fixture states none";
  EXPECT_EQ(species.sourced_values().size(), 20U) << "five from [energy] and fifteen from [intake]";
  EXPECT_EQ(species.weakest_status(), Provenance::Direct);
  EXPECT_TRUE(species.fully_evidenced());
}

TEST(SpeciesConfigTest, OnePlaceholderIntakeParameterReachesTheAggregate) {
  // The case the defect allowed through: everything published except the one
  // coefficient the whole intake ceiling turns on, and reported as evidenced.
  const SpeciesDefinition species =
      parse(weaken(evidenced_species(), kDirectAppetiteScalar,
                   R"(appetite_scalar_per_day = { value = 0.04, status = "placeholder" })"));

  EXPECT_EQ(species.weakest_status(), Provenance::Placeholder);
  EXPECT_FALSE(species.fully_evidenced());
}

// The repository has no `unresolved` status: `provenance_from_string` takes
// direct, derived, verify, fitted and placeholder, and `verify` is what it uses
// for a number whose reading is not yet confirmed. E96 used "unresolved" as
// prose rather than as an enumerator, so this is that case under its real name.
TEST(SpeciesConfigTest, AnUnconfirmedIntakeParameterReachesTheAggregate) {
  const SpeciesDefinition species =
      parse(weaken(evidenced_species(), kDirectConditionLimit,
                   R"(condition_intake_limit = { value = 1.5, status = "verify" })"));

  EXPECT_EQ(species.weakest_status(), Provenance::Verify);
  EXPECT_FALSE(species.fully_evidenced()) << "verify is not evidence";
}

// **The ordering rule is unchanged and still decides.** Provenance runs Direct,
// Derived, Verify, Fitted, Placeholder with the largest winning, so a
// placeholder in [energy] outranks a verify in [intake] exactly as it would
// have before this widened - neither table is privileged over the other.
TEST(SpeciesConfigTest, AWeakerEnergyParameterStillDominatesTheIntakeTable) {
  std::string text = weaken(evidenced_species(), kDirectConditionLimit,
                            R"(condition_intake_limit = { value = 1.5, status = "verify" })");
  text = weaken(std::move(text), kDirectGrazingCoefficient,
                R"(grazing_coefficient = { value = 0.0025, status = "placeholder" })");

  const SpeciesDefinition species = parse(text);

  EXPECT_EQ(species.weakest_status(), Provenance::Placeholder)
      << "placeholder outranks verify wherever each of them sits";
}

// **An undeclared table is no claim, not a weak one.** This is the half of the
// fix that stops it over-correcting: `SourcedValue` defaults to Placeholder, so
// counting an absent `[reproduction]` would report the dry cow as resting on
// five invented numbers when what she actually does is not breed.
TEST(SpeciesConfigTest, AnUndeclaredTableIsNotCountedAgainstTheSpecies) {
  const SpeciesDefinition without = parse(kEvidencedEnergy);

  EXPECT_FALSE(without.declares_intake);
  EXPECT_FALSE(without.declares_reproduction);
  EXPECT_EQ(without.sourced_values().size(), 5U) << "only the required [energy] table";
  EXPECT_EQ(without.weakest_status(), Provenance::Direct);
  EXPECT_TRUE(without.fully_evidenced())
      << "a file that claims only [energy], and supports all of it, is fully evidenced";
}

// **What the one production consumer asks.** `SetupPanel::caveat()` warns when
// the weakest status is neither Direct nor Derived, and before E111 a species
// whose only weakness was in [intake] answered Direct, so no warning appeared.
TEST(SpeciesConfigTest, AWeaknessOnlyInIntakeNowRaisesTheCaveatCondition) {
  const auto quotable = [](const SpeciesDefinition& species) {
    const Provenance weakest = species.weakest_status();
    return weakest == Provenance::Direct || weakest == Provenance::Derived;
  };

  EXPECT_TRUE(quotable(parse(evidenced_species())));
  EXPECT_FALSE(quotable(
      parse(weaken(evidenced_species(), kDirectSizeCoefficient,
                   R"(appetite_size_coefficient = { value = 1.7, status = "placeholder" })"))))
      << "the GUI caveat is the only thing reading this aggregate, and it was being told a "
         "species with a placeholder appetite coefficient was quotable";
}

}  // namespace
}  // namespace paddock::config
