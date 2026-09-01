// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <string>
#include <toml++/toml.hpp>
#include <utility>
#include <vector>

#include <paddock/config/NitrogenReport.hpp>

#include "TomlSupport.hpp"

namespace paddock::config {

namespace {

std::string fixed(double value, int places) {
  std::ostringstream out;
  out << std::fixed << std::setprecision(places) << value;
  return out.str();
}

/// Padded right, so a column of numbers lines up under its heading.
std::string right(const std::string& text, std::size_t width) {
  return text.size() >= width ? text : std::string(width - text.size(), ' ') + text;
}

std::string left(const std::string& text, std::size_t width) {
  return text.size() >= width ? text : text + std::string(width - text.size(), ' ');
}

/// The exclusions, in one place, because every report that quotes a compliance
/// figure has to carry them and a figure without them is worse than none.
std::string what_this_does_not_count() {
  return "  This figure counts nitrate leaving the root zone from urine patches, and nothing "
         "else.\n"
         "  Not counted: leaching from between the patches - dung, fertiliser and soil organic\n"
         "  matter - which OVERSEER puts at under 15% of a grazed block's loss, so this reads\n"
         "  low by about that much; runoff; nitrogen deposited straight into water by stock;\n"
         "  artificial drainage; and any attenuation between the root zone and a waterway.\n";
}

}  // namespace

std::string NitrogenRegulation::validation_error() const {
  if (name.empty()) {
    return "a regulation needs a name";
  }
  if (zone.empty()) {
    return "a regulation needs the zone it applies to - there is no national limit to fall back "
           "on";
  }
  if (leaching_trigger_kg_n_per_ha_per_year.value <= 0.0) {
    return "the leaching trigger must be positive";
  }
  std::string trigger_error =
      leaching_trigger_kg_n_per_ha_per_year.validation_error("leaching trigger");
  if (!trigger_error.empty()) {
    return trigger_error;
  }
  return {};
}

NitrogenRegulation load_nitrogen_regulation(const std::string& path) {
  const toml::table root = detail::parse_file(path);
  const toml::table& regulation = detail::require_table(root, "regulation", path);
  detail::reject_unknown_keys(
      regulation,
      {"name", "authority", "plan", "zone", "nitrogen_leaching_trigger_kg_n_per_ha_per_year",
       "source", "status", "synthetic_nitrogen_fertiliser_cap_kg_n_per_ha_per_year", "cap_source",
       "cap_status"},
      path, "[regulation]");

  NitrogenRegulation rule;
  rule.name = detail::require_string(regulation, "name", path);
  rule.authority = detail::optional_string(regulation, "authority", "");
  rule.plan = detail::optional_string(regulation, "plan", "");
  rule.zone = detail::optional_string(regulation, "zone", "");

  rule.leaching_trigger_kg_n_per_ha_per_year.value =
      detail::require_double(regulation, "nitrogen_leaching_trigger_kg_n_per_ha_per_year", path);
  if (!provenance_from_string(detail::optional_string(regulation, "status", "placeholder"),
                              rule.leaching_trigger_kg_n_per_ha_per_year.status)) {
    detail::throw_in(regulation, path, "status is not a provenance this project recognises");
  }
  rule.source_url = detail::optional_string(regulation, "source", "");
  rule.leaching_trigger_kg_n_per_ha_per_year.source_id = rule.source_url;

  rule.fertiliser_cap_kg_n_per_ha_per_year.value = detail::optional_double(
      regulation, "synthetic_nitrogen_fertiliser_cap_kg_n_per_ha_per_year", 0.0, path);
  if (!provenance_from_string(detail::optional_string(regulation, "cap_status", "placeholder"),
                              rule.fertiliser_cap_kg_n_per_ha_per_year.status)) {
    detail::throw_in(regulation, path, "cap_status is not a provenance this project recognises");
  }
  rule.cap_source = detail::optional_string(regulation, "cap_source", "");
  rule.fertiliser_cap_kg_n_per_ha_per_year.source_id = rule.cap_source;

  detail::require_valid(rule.validation_error(), regulation, path);
  return rule;
}

std::string to_string(NitrogenStanding standing) {
  switch (standing) {
    case NitrogenStanding::Comfortable:
      return "under the trigger";
    case NitrogenStanding::Close:
      return "within a tenth of the trigger";
    case NitrogenStanding::OverTheTrigger:
      return "over the trigger";
  }
  return "unknown";
}

NitrogenStanding NitrogenYear::standing(const NitrogenRegulation& rule) const {
  const double trigger = rule.leaching_trigger_kg_n_per_ha_per_year.value;
  if (leached_kg_n_per_ha > trigger) {
    return NitrogenStanding::OverTheTrigger;
  }
  return leached_kg_n_per_ha > trigger * 0.9 ? NitrogenStanding::Close
                                             : NitrogenStanding::Comfortable;
}

NitrogenYear nitrogen_year(const RunSummary& run, std::string label) {
  NitrogenYear year;
  year.label = std::move(label);
  if (!run.dates.empty()) {
    year.range = core::DateRange{run.dates.front(), run.dates.back()};
  }

  year.leached_kg_n_per_ha = run.nitrate_leached_total_kg_per_ha();

  for (const core::ProcessEntry& entry : run.ledger.entries(core::Budget::Water)) {
    if (entry.process == "drainage") {
      year.drainage_mm = entry.outflow;
    }
    if (entry.process == "rainfall") {
      year.rainfall_mm = entry.inflow;
    }
  }
  for (const core::ProcessEntry& entry : run.ledger.entries(core::Budget::Nitrogen)) {
    if (entry.process == "legume_fixation") {
      year.fixed_kg_n_per_ha = entry.inflow;
    }
    if (entry.process == "excreta_returned") {
      year.excreta_returned_kg_n_per_ha = entry.inflow;
    }
    if (entry.process == "nitrate_leaching") {
      year.leached_from_patches_kg_n_per_ha = entry.outflow;
    }
    if (entry.process == "nitrate_leaching_inter_patch") {
      year.leached_between_patches_kg_n_per_ha = entry.outflow;
    }
    if (entry.process == "fertiliser") {
      year.fertiliser_kg_n_per_ha = entry.inflow;
    }
  }
  return year;
}

std::string nitrogen_compliance_report(const NitrogenYear& year, const NitrogenRegulation& rule) {
  const double trigger = rule.leaching_trigger_kg_n_per_ha_per_year.value;

  std::ostringstream out;
  out << "Nitrogen loss to water - " << year.label << "\n";
  out << "  " << year.range.first.to_iso_string() << " to " << year.range.last.to_iso_string()
      << "\n\n";

  out << "  Leached past the root zone   " << right(fixed(year.leached_kg_n_per_ha, 1), 8)
      << " kg N/ha\n";
  out << "  " << rule.zone << " trigger          " << right(fixed(trigger, 1), 8) << " kg N/ha\n";

  // **The verdict, worded as the rule actually works.** A farm over this figure
  // is not in breach: it is in the group the plan asks to reduce against its own
  // baseline. Printing "non-compliant" would be overstating the regulation.
  switch (year.standing(rule)) {
    case NitrogenStanding::OverTheTrigger:
      out << "\n  OVER THE TRIGGER by " << fixed(year.leached_kg_n_per_ha - trigger, 1)
          << " kg N/ha.\n"
          << "  " << rule.authority << " requires farms over " << fixed(trigger, 0)
          << " kg N/ha/yr in the\n"
          << "  " << rule.zone << " zone to reduce below their own nitrogen baseline. This is\n"
          << "  not a breach on its own; it is what puts a farm in that group.\n";
      break;
    case NitrogenStanding::Close:
      out << "\n  UNDER THE TRIGGER, but within a tenth of it. A wetter year would very likely\n"
          << "  put this farm over: leaching here moves with drainage, and drainage is weather.\n";
      break;
    case NitrogenStanding::Comfortable:
      out << "\n  UNDER THE TRIGGER, with room. \n";
      break;
  }

  out << "\n  Where it came from\n";
  out << "    Rain                       " << right(fixed(year.rainfall_mm, 0), 8) << " mm\n";
  out << "    Drained past the root zone " << right(fixed(year.drainage_mm, 0), 8) << " mm\n";
  out << "    Leached per mm of drainage " << right(fixed(year.kg_n_per_mm_drainage(), 3), 8)
      << " kg N/ha/mm\n";
  // **The two halves, apart**, because they are not the same problem: patch
  // leaching is what stocking and grazing move, and inter-patch leaching is
  // mostly what the soil does on its own.
  out << "    From urine patches         "
      << right(fixed(year.leached_from_patches_kg_n_per_ha, 1), 8) << " kg N/ha\n";
  out << "    From between the patches   "
      << right(fixed(year.leached_between_patches_kg_n_per_ha, 1), 8) << " kg N/ha  ("
      << fixed(year.inter_patch_share() * 100.0, 0)
      << "% of the loss; OVERSEER puts it under 15)\n";
  out << "    Returned as dung and urine " << right(fixed(year.excreta_returned_kg_n_per_ha, 1), 8)
      << " kg N/ha\n";
  out << "    Fixed by clover            " << right(fixed(year.fixed_kg_n_per_ha, 1), 8)
      << " kg N/ha\n";
  out << "    Synthetic N fertiliser     " << right(fixed(year.fertiliser_kg_n_per_ha, 1), 8)
      << " kg N/ha  (national cap " << fixed(rule.fertiliser_cap_kg_n_per_ha_per_year.value, 0)
      << ")\n";

  out << "\n  What this figure does not count\n" << what_this_does_not_count();

  if (!rule.source_url.empty()) {
    out << "\n  Trigger: " << rule.authority << ", " << rule.plan << "\n  " << rule.source_url
        << "\n";
  }
  return out.str();
}

std::string nitrogen_years_report(const std::vector<NitrogenYear>& years,
                                  const NitrogenRegulation& rule) {
  const double trigger = rule.leaching_trigger_kg_n_per_ha_per_year.value;

  std::ostringstream out;
  out << "Nitrogen loss to water, year by year - " << rule.zone << " trigger " << fixed(trigger, 0)
      << " kg N/ha/yr\n\n";
  out << "  " << left("year", 14) << right("rain", 7) << right("drained", 9) << right("leached", 9)
      << right("per mm", 9) << "  standing\n";
  out << "  " << std::string(14 + 7 + 9 + 9 + 9 + 2 + 26, '-') << "\n";

  for (const NitrogenYear& year : years) {
    out << "  " << left(year.label, 14) << right(fixed(year.rainfall_mm, 0), 7)
        << right(fixed(year.drainage_mm, 0), 9) << right(fixed(year.leached_kg_n_per_ha, 1), 9)
        << right(fixed(year.kg_n_per_mm_drainage(), 3), 9) << "  " << to_string(year.standing(rule))
        << "\n";
  }

  // **The column that separates a wet year from a leaky farm.** Leaching per
  // millimetre of drainage holds the weather roughly constant: a farm whose
  // loss doubled in a year that drained three times as much is not leaking
  // harder, it is draining harder. A council reading only the leached column
  // could not tell those apart.
  out << "\n  Read the 'per mm' column before the 'leached' one. Leaching moves with drainage,\n"
      << "  and drainage is weather rather than management - a farm can leach twice as much in\n"
      << "  a wet year without having changed anything it does. What management moves is the\n"
      << "  nitrogen available to leach, which shows up per millimetre.\n";

  out << "\n  What these figures do not count\n" << what_this_does_not_count();
  return out.str();
}

}  // namespace paddock::config
