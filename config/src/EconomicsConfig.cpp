// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#include <algorithm>
#include <filesystem>
#include <string>
#include <system_error>
#include <toml++/toml.hpp>
#include <utility>
#include <vector>

#include <paddock/config/ConfigError.hpp>
#include <paddock/config/EconomicsConfig.hpp>

#include "TomlSupport.hpp"

namespace paddock::config {

namespace {

/// A value with its status and its source, the way every other data file in
/// this project states one. **Status has no default on purpose**: defaulting it
/// to placeholder would let a real citation go unrecorded, and defaulting it to
/// direct would let an invention pass as one.
SourcedValue read_sourced(const toml::table& parent, std::string_view key,
                          const std::string& path) {
  const toml::table& entry = detail::require_table(parent, key, path);
  detail::reject_unknown_keys(entry, {"value", "status", "source"}, path,
                              "'" + std::string(key) + "'");

  SourcedValue sourced;
  sourced.value = detail::require_double(entry, "value", path);

  const std::string status = detail::require_string(entry, "status", path);
  if (!provenance_from_string(status, sourced.status)) {
    detail::throw_in(entry, path,
                     "'" + std::string(key) + "' has status '" + status +
                         "', which is not one this project recognises");
  }
  sourced.source_id = detail::optional_string(entry, "source", "");
  if (sourced.source_id == "none") {
    sourced.source_id.clear();
  }

  const std::string trouble = sourced.validation_error(std::string(key));
  if (!trouble.empty()) {
    detail::throw_in(entry, path, trouble);
  }
  return sourced;
}

}  // namespace

Provenance FarmEconomics::weakest_status() const {
  // The enumerators run from strongest to weakest, so the largest wins - the
  // same reading SpeciesDefinition uses.
  Provenance weakest = Provenance::Direct;
  for (const auto& entry : provenance) {
    if (static_cast<int>(entry.second.status) > static_cast<int>(weakest)) {
      weakest = entry.second.status;
    }
  }
  return weakest;
}

std::string FarmEconomics::validation_error() const {
  if (name.empty()) {
    return "an economics file needs a name";
  }
  if (costs.annual_per_hectare() <= 0.0) {
    return "a farm with no operating costs is not a farm";
  }
  if (prices.lamb_dollars_per_kg_carcass <= 0.0) {
    return "a lamb has to be worth something";
  }
  if (opening_balance_per_hectare < 0.0) {
    return "a farm cannot open the year owing money it has not borrowed - this model has no debt";
  }
  return {};
}

FarmEconomics load_economics(const std::string& path) {
  const toml::table root = detail::parse_file(path);
  detail::reject_unknown_keys(
      root, {"economics", "expenditure", "expenditure_options", "prices", "capital", "sources"},
      path, "the file");

  FarmEconomics economics;

  const toml::table& header = detail::require_table(root, "economics", path);
  detail::reject_unknown_keys(
      header, {"name", "display_name", "description", "region", "survey_class", "survey_year"},
      path, "[economics]");
  economics.name = detail::require_string(header, "name", path);
  economics.display_name = detail::optional_string(header, "display_name", "");
  economics.description = detail::optional_string(header, "description", "");
  economics.region = detail::optional_string(header, "region", "");
  economics.survey_class = detail::optional_string(header, "survey_class", "");
  economics.survey_year = detail::optional_string(header, "survey_year", "");

  const toml::table& expenditure = detail::require_table(root, "expenditure", path);

  // **Named one by one rather than iterated.** A loop over the table's keys
  // would accept a misspelled line by silently ignoring it, and a farm quietly
  // missing its fertiliser bill still balances its books - it just reports a
  // margin nobody could achieve.
  const auto cost = [&](std::string_view key, double& into) {
    const SourcedValue value = read_sourced(expenditure, key, path);
    into = value.value;
    economics.provenance.emplace_back(std::string(key), value);
  };

  cost("wages_and_salaries", economics.costs.wages_and_salaries);
  cost("animal_health", economics.costs.animal_health);
  cost("weed_and_pest", economics.costs.weed_and_pest);
  cost("shearing", economics.costs.shearing);
  cost("fertiliser", economics.costs.fertiliser);
  cost("lime", economics.costs.lime);
  cost("seeds", economics.costs.seeds);
  cost("vehicles_and_fuel", economics.costs.vehicles_and_fuel);
  cost("electricity", economics.costs.electricity);
  cost("feed_and_grazing", economics.costs.feed_and_grazing);
  cost("dog_expenses", economics.costs.dog_expenses);
  cost("cultivation_and_sowing", economics.costs.cultivation_and_sowing);
  cost("cash_crop", economics.costs.cash_crop);
  cost("repairs_and_maintenance", economics.costs.repairs_and_maintenance);
  cost("irrigation_charges", economics.costs.irrigation_charges);
  cost("cartage", economics.costs.cartage);
  cost("administration", economics.costs.administration);
  cost("insurance_and_acc", economics.costs.insurance_and_acc);
  cost("rates", economics.costs.rates);
  cost("interest", economics.costs.interest);
  cost("rent", economics.costs.rent);
  cost("depreciation", economics.costs.depreciation);

  detail::reject_unknown_keys(expenditure,
                              {"wages_and_salaries",
                               "animal_health",
                               "weed_and_pest",
                               "shearing",
                               "fertiliser",
                               "lime",
                               "seeds",
                               "vehicles_and_fuel",
                               "electricity",
                               "feed_and_grazing",
                               "dog_expenses",
                               "cultivation_and_sowing",
                               "cash_crop",
                               "repairs_and_maintenance",
                               "irrigation_charges",
                               "cartage",
                               "administration",
                               "insurance_and_acc",
                               "rates",
                               "interest",
                               "rent",
                               "depreciation"},
                              path, "[expenditure]");

  // **Interest, rent and depreciation are carried and not charged by default.**
  // They are real costs and they are not operating costs; a run that charged
  // them would be reporting against a balance sheet nobody stated. The file
  // says which, and this reads it rather than assuming.
  if (const toml::table* options = root["expenditure_options"].as_table()) {
    detail::reject_unknown_keys(*options, {"charge_interest", "charge_rent", "charge_depreciation"},
                                path, "[expenditure_options]");
    economics.costs.charge_interest =
        detail::optional_bool(*options, "charge_interest", false, path);
    economics.costs.charge_rent = detail::optional_bool(*options, "charge_rent", false, path);
    economics.costs.charge_depreciation =
        detail::optional_bool(*options, "charge_depreciation", false, path);
  }

  const toml::table& prices = detail::require_table(root, "prices", path);
  detail::reject_unknown_keys(
      prices, {"lamb_dollars_per_kg_carcass", "wool_dollars_per_kg", "cull_ewe_dollars_per_head"},
      path, "[prices]");

  const auto price = [&](std::string_view key, double& into) {
    const SourcedValue value = read_sourced(prices, key, path);
    into = value.value;
    economics.provenance.emplace_back(std::string(key), value);
  };
  price("lamb_dollars_per_kg_carcass", economics.prices.lamb_dollars_per_kg_carcass);
  price("wool_dollars_per_kg", economics.prices.wool_dollars_per_kg);
  price("cull_ewe_dollars_per_head", economics.prices.cull_ewe_dollars_per_head);

  if (const toml::table* capital = root["capital"].as_table()) {
    detail::reject_unknown_keys(*capital, {"opening_balance_per_hectare"}, path, "[capital]");
    const SourcedValue opening = read_sourced(*capital, "opening_balance_per_hectare", path);
    economics.opening_balance_per_hectare = opening.value;
    economics.provenance.emplace_back("opening_balance_per_hectare", opening);
  }

  detail::require_valid(economics.validation_error(), header, path);
  return economics;
}

FarmBusiness business_from(const ScenarioBundle& bundle, const FarmEconomics& economics) {
  FarmBusiness business;
  business.costs = economics.costs;
  business.prices = economics.prices;

  // Cash on day one is stated per hectare, so it scales with the farm rather
  // than being a number that happens to suit one block.
  double hectares = 0.0;
  if (bundle.grid.has_value()) {
    hectares = static_cast<double>(bundle.grid->cols) * static_cast<double>(bundle.grid->rows) *
               bundle.grid->cell_size_m * bundle.grid->cell_size_m / 10'000.0;
  }
  business.opening_balance_dollars = economics.opening_balance_per_hectare * hectares;

  if (bundle.mobs.empty()) {
    return business;
  }

  // **The age structure is an assumption, and it is stated here rather than
  // buried.** A bundle gives a head count; a flock has classes. This splits the
  // first mob evenly across the breeding ages, two to the cull age, which is
  // not what a real flock looks like - a real one is heavier in the young
  // classes because the old ones have been culled out of it. What it does get
  // right is that the flock has an age structure at all, so the oldest draft
  // leaves each July and the replacements come from the crop.
  const MobSpec& first = bundle.mobs.front();
  const core::FlockRates rates = business.rates;
  const int classes = std::max(1, rates.cull_age_years - 1);
  const int per_class = std::max(0, first.head / classes);

  const int first_year = bundle.range.first.year;
  for (int age = 2; age <= rates.cull_age_years; ++age) {
    if (per_class <= 0) {
      break;
    }
    core::AgeCohort cohort;
    cohort.birth_year = first_year - age;
    cohort.age_years = age;
    cohort.mob.name = "ewes " + std::to_string(age);
    cohort.mob.head = per_class;
    cohort.mob.animal = first.animal;
    cohort.mob.state.liveweight_kg = first.liveweight_kg;
    cohort.mob.state.age_days = 365.0 * age;
    business.flock.add(std::move(cohort));
  }
  return business;
}

std::vector<FarmEconomics> load_economics_directory(const std::string& directory) {
  namespace fs = std::filesystem;

  std::error_code error;
  if (!fs::is_directory(directory, error)) {
    throw ConfigError(directory, 1, 1, "not a directory of economics definitions");
  }

  // Sorted before loading, like the species directory and for the same reason:
  // a directory iterator's order is the filesystem's, and a list that changed
  // order between machines would make every report that prints it
  // non-reproducible.
  std::vector<std::string> files;
  for (const fs::directory_entry& entry : fs::directory_iterator(directory)) {
    if (entry.is_regular_file() && entry.path().extension() == ".toml") {
      files.push_back(entry.path().string());
    }
  }
  std::sort(files.begin(), files.end());

  std::vector<FarmEconomics> loaded;
  loaded.reserve(files.size());
  for (const std::string& file : files) {
    loaded.push_back(load_economics(file));
  }
  return loaded;
}

}  // namespace paddock::config
