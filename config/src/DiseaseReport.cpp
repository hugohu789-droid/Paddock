// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include <paddock/config/DiseaseReport.hpp>

namespace paddock::config {

namespace {

std::string fixed(double value, int places) {
  std::ostringstream out;
  out << std::fixed << std::setprecision(places) << value;
  return out.str();
}

/// Thousands separators, because a reader comparing 461158 with 54000 at a
/// glance should not have to count digits.
std::string grouped(double value) {
  std::string digits = fixed(std::max(0.0, value), 0);
  for (int position = static_cast<int>(digits.size()) - 3; position > 0; position -= 3) {
    digits.insert(static_cast<std::size_t>(position), ",");
  }
  return digits;
}

/// What to print in the start column.
///
/// Three cases, and the third is why this is a function rather than a nested
/// conditional: a programme that opened in one year and is still running in the
/// next has no start date of its own, and printing the unset one gave
/// 1970-01-01 in a report while every test passed.
std::string programme_start(const core::MycotoxinYear& year) {
  if (year.started_this_year) {
    return year.programme_started.to_iso_string();
  }
  return year.zinc_programme_days > 0 ? "carried in" : "—";
}

/// "2016-17", the way a farm year is written.
std::string farm_year(int starting_year) {
  const std::string next = std::to_string(starting_year + 1);
  return std::to_string(starting_year) + "-" + next.substr(next.size() - 2);
}

std::vector<core::MycotoxinYear> years_of(const DiseaseSite& site,
                                          const DiseaseDefinition& disease) {
  return core::mycotoxin_years(site.weather, disease.monitor_own_farm_spores_per_g,
                               disease.dangerous_spores_per_g, disease.mycotoxin);
}

struct Summary {
  int years = 0;
  int years_with_a_programme = 0;
  int total_zinc_days = 0;
  int worst_zinc_days = 0;
  int worst_year = 0;
  double highest_ggt = 0.0;
  double highest_peak = 0.0;
};

Summary summarise(const std::vector<core::MycotoxinYear>& years) {
  Summary total;
  total.years = static_cast<int>(years.size());
  for (const core::MycotoxinYear& year : years) {
    if (year.zinc_programme_days > 0) {
      ++total.years_with_a_programme;
    }
    total.total_zinc_days += year.zinc_programme_days;
    if (year.zinc_programme_days > total.worst_zinc_days) {
      total.worst_zinc_days = year.zinc_programme_days;
      total.worst_year = year.starting_year;
    }
    total.highest_ggt = std::max(total.highest_ggt, year.peak_ggt_iu_per_l);
    total.highest_peak = std::max(total.highest_peak, year.peak_spores_per_g);
  }
  return total;
}

/// The sentence every one of these reports has to carry.
void write_caveat(std::ostringstream& out, const DiseaseDefinition& disease) {
  out << "\n> **The model does not give the animals zinc.** The programme columns are what "
      << "DairyNZ's protocol would have told a farmer to do on this weather - a full dose from "
      << grouped(disease.full_zinc_dose_spores_per_g) << " spores/g, ending only after "
      << disease.mycotoxin.stand_down_weeks << " weeks at or below "
      << grouped(disease.stand_down_spores_per_g)
      << ". The serum GGT beside them is what the season would have done to a mob nobody "
      << "treated. Read together they say what was demanded and what was at stake, not what "
      << "happened to a farm that followed the advice.\n";
}

}  // namespace

std::string render_disease_years(const DiseaseSite& site, const DiseaseDefinition& disease) {
  const std::vector<core::MycotoxinYear> years = years_of(site, disease);
  const Summary total = summarise(years);

  std::ostringstream out;
  out << "# " << disease.display_name << " at " << site.name << "\n\n";

  if (years.empty()) {
    out << "No weather to report on.\n";
    return out.str();
  }

  // **The lead is the decision, not the measurement.** A reader who stops after
  // one sentence should still have the answer.
  out << "Over " << total.years << (total.years == 1 ? " year" : " years")
      << ", a zinc programme would have run in **" << total.years_with_a_programme
      << (total.years == 1 ? "** of it" : "** of them");
  if (total.worst_zinc_days > 0) {
    out << ", the longest **" << total.worst_zinc_days << " days** in "
        << farm_year(total.worst_year);
  }
  out << ".\n\n";

  out << "| Year | Peak spores/g | Days over monitoring | Zinc days | Programme starts | "
      << "Worst GGT untreated |\n";
  out << "|---|---:|---:|---:|---|---:|\n";
  for (const core::MycotoxinYear& year : years) {
    out << "| " << farm_year(year.starting_year) << " | " << grouped(year.peak_spores_per_g)
        << " | " << year.days_at_or_above_monitoring << " | " << year.zinc_programme_days << " | "
        << programme_start(year) << " | " << fixed(year.peak_ggt_iu_per_l, 0) << " |\n";
  }

  out << "\nThe monitoring column counts days at or above "
      << grouped(disease.monitor_own_farm_spores_per_g)
      << " spores/g, where the guidance asks a farmer to start counting their own paddocks.\n";
  write_caveat(out, disease);
  return out.str();
}

std::string render_disease_comparison(const std::vector<DiseaseSite>& sites,
                                      const DiseaseDefinition& disease) {
  std::ostringstream out;
  out << "# " << disease.display_name << ", farm against farm\n\n";

  if (sites.empty()) {
    out << "No farms to compare.\n";
    return out.str();
  }

  std::vector<Summary> totals;
  totals.reserve(sites.size());
  for (const DiseaseSite& site : sites) {
    totals.push_back(summarise(years_of(site, disease)));
  }

  // **Summary statistics only.** Two ten-row tables side by side is not a
  // comparison, it is two tables; what a reader wants here is whether this is a
  // question for their farm at all.
  out << "| |";
  for (const DiseaseSite& site : sites) {
    out << " " << site.name << " |";
  }
  out << "\n|---|";
  for (std::size_t i = 0; i < sites.size(); ++i) {
    out << "---:|";
  }
  out << "\n";

  const auto row = [&](const std::string& label, const auto& cell) {
    out << "| " << label << " |";
    for (const Summary& total : totals) {
      out << " " << cell(total) << " |";
    }
    out << "\n";
  };

  row("Years needing a programme", [](const Summary& t) {
    return std::to_string(t.years_with_a_programme) + " of " + std::to_string(t.years);
  });
  row("Zinc days over the decade",
      [](const Summary& t) { return std::to_string(t.total_zinc_days); });
  row("Worst year", [](const Summary& t) {
    return t.worst_zinc_days > 0
               ? farm_year(t.worst_year) + ", " + std::to_string(t.worst_zinc_days) + " days"
               : std::string("—");
  });
  row("Highest spore count", [](const Summary& t) { return grouped(t.highest_peak); });
  row("Highest GGT untreated", [](const Summary& t) { return fixed(t.highest_ggt, 0); });

  out << "\nThe same years and the same disease file at every site, so a difference here is a "
      << "difference between climates.\n";
  write_caveat(out, disease);
  return out.str();
}

}  // namespace paddock::config
