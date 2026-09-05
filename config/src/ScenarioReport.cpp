// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#include <algorithm>
#include <array>
#include <cstddef>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <paddock/config/ScenarioReport.hpp>

namespace paddock::config {

namespace {

std::string fixed(double value, int places) {
  std::ostringstream out;
  out << std::fixed << std::setprecision(places) << value;
  return out.str();
}

/// A month of a run, for a reader who wants the shape of a year rather than
/// three hundred and sixty-five rows of it.
struct Month {
  int year = 0;
  int month = 0;
  double cover_total = 0.0;
  double cover_low = 1e18;
  double cover_high = 0.0;
  double closing_liveweight = 0.0;
  double bought_kg_dm = 0.0;
  int days = 0;
};

std::vector<Month> by_month(const RunSummary& run) {
  std::vector<Month> months;
  for (std::size_t day = 0; day < run.dates.size(); ++day) {
    const core::Date& date = run.dates[day];
    if (months.empty() || months.back().year != date.year || months.back().month != date.month) {
      Month next;
      next.year = date.year;
      next.month = date.month;
      months.push_back(next);
    }
    Month& current = months.back();
    const double cover = run.cover_kg_dm_per_ha[day];
    current.cover_total += cover;
    current.cover_low = std::min(current.cover_low, cover);
    current.cover_high = std::max(current.cover_high, cover);
    // **The liveweight series is not always as long as the year.** A run with
    // no stock on it produces no liveweight at all, while its dates still cover
    // every day, so indexing one by the other walked off the end of an empty
    // vector - which is not a wrong number in a report but a crash before there
    // is one. The month keeps the zero it was built with.
    if (day < run.liveweight_kg.size()) {
      current.closing_liveweight = run.liveweight_kg[day];
    }
    ++current.days;
  }

  for (const core::FeedPurchase& purchase : run.purchases) {
    for (Month& month : months) {
      if (month.year == purchase.date.year && month.month == purchase.date.month) {
        month.bought_kg_dm += purchase.kg_dm;
        break;
      }
    }
  }
  return months;
}

std::string_view month_name(int month) {
  static constexpr std::array<std::string_view, 13> kNames{
      "",     "January", "February",  "March",   "April",    "May",     "June",
      "July", "August",  "September", "October", "November", "December"};
  return (month >= 1 && month <= 12) ? kNames.at(static_cast<std::size_t>(month))
                                     : std::string_view{"?"};
}

void write_heading(std::ostringstream& out, const ScenarioBundle& bundle, const RunSummary& run,
                   const ReportOptions& options) {
  const std::string name = options.farm_name.empty() ? bundle.name : options.farm_name;
  out << "# " << name << " — simulated year\n\n";
  out << "**" << run.label << "**, " << bundle.range.first.to_iso_string() << " to "
      << bundle.range.last.to_iso_string() << " (" << run.dates.size() << " days).\n\n";
  if (!bundle.description.empty()) {
    out << bundle.description << "\n\n";
  }
}

void write_what_was_simulated(std::ostringstream& out, const ScenarioBundle& bundle,
                              const ReportOptions& options) {
  out << "## What was simulated\n\n";

  double hectares = 0.0;
  std::size_t paddocks = 0;
  if (bundle.grid.has_value()) {
    const GridSpec& grid = *bundle.grid;
    hectares =
        static_cast<double>(grid.cols * grid.rows) * grid.cell_size_m * grid.cell_size_m / 10000.0;
    paddocks = bundle.make_paddocks().size();
  }

  out << "| | |\n|---|---|\n";
  out << "| Area | " << fixed(hectares, 1) << " ha |\n";
  out << "| Paddocks | " << paddocks << " |\n";

  // The ground, because it decides whether two sourced pieces of the model ran
  // at all: what a slope costs an animal to walk, and what radiation a slope
  // receives. A reader cannot tell from any other number in this report.
  if (bundle.terrain.is_flat()) {
    out << "| Ground | modelled flat";
    if (!options.ground_caveat.empty()) {
      out << " — " << options.ground_caveat;
    }
    out << " |\n";
  } else {
    out << "| Ground | synthetic surface, falling "
        << fixed(bundle.terrain.surface.gradient_east * 100.0, 1) << "% east and "
        << fixed(bundle.terrain.surface.gradient_north * 100.0, 1) << "% north |\n";
  }

  // Where the fences came from, on the same footing as where the ground came
  // from. A report that names a real farm and shows paddock figures should say
  // which of the two the paddocks are.
  if (const std::string fences = bundle.paddock_caveat(); !fences.empty()) {
    out << "| Paddocks | " << fences << " |\n";
  }

  int head = 0;
  for (const MobSpec& mob : bundle.mobs) {
    head += mob.head;
    out << "| Mob | " << mob.name << ", " << mob.head << " head of " << mob.animal.class_id
        << ", starting at " << fixed(mob.liveweight_kg, 1) << " kg |\n";
  }
  if (hectares > 0.0 && head > 0) {
    out << "| Stocking rate | " << fixed(static_cast<double>(head) / hectares, 2)
        << " head per hectare |\n";
  }

  if (options.policy != nullptr) {
    const core::ManagementPolicy& policy = *options.policy;
    out << "| Management | the farmer chose the system day by day |\n";
    out << "| Cover floor | " << fixed(policy.minimum_cover_kg_dm_per_ha, 0)
        << " kg DM/ha — below this the farmer feeds out rather than grazing harder |\n";
    out << "| Rotates above | " << fixed(policy.rotation_cover_threshold_kg_dm_per_ha, 0)
        << " kg DM/ha |\n";
    out << "| Target gain | " << fixed(policy.target_liveweight_gain_kg_per_day, 3)
        << " kg per head per day |\n";
    out << "| May buy feed | " << (policy.may_buy_feed ? "yes" : "no") << " |\n";
    // Which system the farmer ran, and what they bought at the floor. Both
    // change the answer, and a reader comparing two reports has no other way to
    // tell which farm was run how.
    out << "| Grazing | " << core::to_string(policy.preference) << " |\n";
    if (policy.may_buy_feed) {
      out << "| At the cover floor | " << core::to_string(policy.floor_purchase) << " |\n";
    }
  } else {
    out << "| Management | a fixed calendar of " << bundle.grazing.periods().size()
        << " periods |\n";
  }
  out << "\n";
}

void write_pasture(std::ostringstream& out, const RunSummary& run,
                   const std::vector<Month>& months) {
  out << "## The pasture\n\n";
  out << "Cover ran between **" << fixed(run.lowest_cover_kg_dm_per_ha(), 0) << "** and **"
      << fixed(run.highest_cover_kg_dm_per_ha(), 0) << " kg DM/ha**, averaging "
      << fixed(run.mean_cover_kg_dm_per_ha(), 0) << ". The farm ended the year at "
      << fixed(run.closing_cover_kg_dm, 0) << ".\n\n";

  out << "| Month | Mean cover | Lowest | Highest |\n|---|---|---|---|\n";
  for (const Month& month : months) {
    out << "| " << month_name(month.month) << " " << month.year << " | "
        << fixed(month.cover_total / static_cast<double>(month.days), 0) << " | "
        << fixed(month.cover_low, 0) << " | " << fixed(month.cover_high, 0) << " |\n";
  }
  out << "\nAll figures kg DM/ha, averaged over every cell of the farm.\n\n";
}

void write_stock(std::ostringstream& out, const RunSummary& run, const std::vector<Month>& months) {
  out << "## The stock\n\n";

  // **A farm can be run with nothing standing on it.** "What would this ground
  // grow if it were not grazed" is a fair question, and the pasture, water and
  // budget sections answer it. What this section must not do is answer it with
  // liveweights: there is no herd, so "0.0 kg to 0.0 kg, a change of 0.00 kg"
  // would be a figure invented to fill a table, and a table of invented figures
  // is how a report stops being worth reading.
  if (run.liveweight_kg.empty()) {
    out << "None. Nothing grazed this farm, so the pasture above is what the ground grew "
           "ungrazed, and there is no feed budget and no farmer's decisions to report: those "
           "sections are left out rather than filled with zeroes.\n\n";
    return;
  }

  const double change = run.liveweight_change_kg();
  out << "Liveweight went from **" << fixed(run.opening_liveweight_kg(), 1) << " kg** to **"
      << fixed(run.closing_liveweight_kg(), 1) << " kg**, a change of " << fixed(change, 2)
      << " kg a head";
  if (!run.dates.empty()) {
    out << " over " << run.dates.size() << " days";
  }
  out << ".\n\n";

  if (run.feed_supply_short_days > 0) {
    out << "The stock could not get what they needed on **" << run.feed_supply_short_days
        << " days**. That is the number to look at before any other: a farm that runs short is "
           "not carrying its stock, whatever the closing weight says.\n\n";
  } else {
    out << "The stock got what they needed on every day of the run.\n\n";
  }

  out << "| Month | Liveweight at month end |\n|---|---|\n";
  for (const Month& month : months) {
    out << "| " << month_name(month.month) << " " << month.year << " | "
        << fixed(month.closing_liveweight, 1) << " kg |\n";
  }
  out << "\n";
}

void write_management(std::ostringstream& out, const RunSummary& run) {
  out << "## What the farmer did\n\n";
  out << "| | |\n|---|---|\n";
  out << "| Mob shifts | " << run.moves << " |\n";
  if (run.short_spells > 0) {
    out << "| Shifts onto ground that had not had its rest | " << run.short_spells << " |\n";
  }
  if (run.grazings_extended > 0) {
    out << "| Times a mob had nowhere to go and stayed put | " << run.grazings_extended << " |\n";
  }

  if (!run.system_each_day.empty()) {
    int rotational = 0;
    for (const core::GrazingSystem system : run.system_each_day) {
      if (system == core::GrazingSystem::Rotational) {
        ++rotational;
      }
    }
    out << "| Days rotating | " << rotational << " of " << run.system_each_day.size() << " |\n";
    out << "| Days set stocked | " << (run.system_each_day.size() - rotational) << " |\n";
  }
  out << "\n";

  if (run.short_spells > 0) {
    out << "Shifts onto under-rested ground are worth reading as a signal rather than a fault: "
           "they mean the farm did not have enough paddocks to hold its own rotation, which is "
           "what Smith and Dawson (1976) call the \"shuffle\".\n\n";
  }
}

void write_bought_feed(std::ostringstream& out, const RunSummary& run) {
  out << "## Bought feed\n\n";

  if (run.purchases.empty()) {
    out << "None. The farm carried its stock on what it grew.\n\n";
    return;
  }

  out << "**" << fixed(run.bought_feed_kg_dm(), 0) << " kg of dry matter** was bought in, across **"
      << run.days_feed_was_bought() << " days**.\n\n";

  // Grouped by month, because a reader wants to know when the farm ran out
  // before they want a list of days - and in calendar order, which a map keyed
  // by the month's name is not: it put June before March.
  struct MonthlyFeed {
    int year = 0;
    int month = 0;
    double kg_dm = 0.0;
    int days = 0;
    std::string reason;
  };

  std::vector<MonthlyFeed> monthly;
  for (const core::FeedPurchase& purchase : run.purchases) {
    auto found = std::find_if(monthly.begin(), monthly.end(), [&](const MonthlyFeed& entry) {
      return entry.year == purchase.date.year && entry.month == purchase.date.month;
    });
    if (found == monthly.end()) {
      monthly.push_back(MonthlyFeed{purchase.date.year, purchase.date.month, 0.0, 0, ""});
      found = std::prev(monthly.end());
    }
    found->kg_dm += purchase.kg_dm;
    ++found->days;
    found->reason = core::to_string(purchase.reason);
  }
  std::sort(monthly.begin(), monthly.end(), [](const MonthlyFeed& lhs, const MonthlyFeed& rhs) {
    return lhs.year != rhs.year ? lhs.year < rhs.year : lhs.month < rhs.month;
  });

  out << "| Month | Bought | Days | Most recent reason |\n|---|---|---|---|\n";
  for (const MonthlyFeed& entry : monthly) {
    out << "| " << month_name(entry.month) << " " << entry.year << " | " << fixed(entry.kg_dm, 0)
        << " kg DM | " << entry.days << " | " << entry.reason << " |\n";
  }
  out << "\n";

  out << "<details>\n<summary>Every purchase, by date</summary>\n\n";
  out << "| Date | Mob | Bought | Reason |\n|---|---|---|---|\n";
  for (const core::FeedPurchase& purchase : run.purchases) {
    out << "| " << purchase.date.to_iso_string() << " | " << purchase.mob_name << " | "
        << fixed(purchase.kg_dm, 0) << " kg DM | " << core::to_string(purchase.reason) << " |\n";
  }
  out << "\n</details>\n\n";

  out << "Bought feed is reported in kilograms of dry matter rather than in money. This project "
         "has no sourced price for feed or for stock, and putting an invented one into a report "
         "would make it look like an answer.\n\n";
}

void write_budgets(std::ostringstream& out, const RunSummary& run) {
  out << "## Did the books balance\n\n";

  constexpr double kTolerance = 1e-9;
  const bool dry_matter =
      run.ledger.closes(core::Budget::DryMatter, run.closing_cover_kg_dm, kTolerance);
  const bool nitrogen =
      run.ledger.closes(core::Budget::Nitrogen, run.closing_nitrogen_kg, kTolerance);
  const bool water = run.ledger.closes(core::Budget::Water, run.closing_water_mm, kTolerance);

  out << "| Budget | Closes |\n|---|---|\n";
  out << "| Dry matter | " << (dry_matter ? "yes" : "**no**") << " |\n";
  out << "| Nitrogen | " << (nitrogen ? "yes" : "**no**") << " |\n";
  out << "| Water | " << (water ? "yes" : "**no**") << " |\n\n";

  if (dry_matter && nitrogen && water) {
    out << "Every kilogram and every millimetre is accounted for to within a billionth. That "
           "does not make the model right, but it does mean nothing in this run appeared or "
           "vanished without being recorded.\n\n";
  } else {
    out << "**One or more budgets did not close, so nothing in this report should be relied "
           "on.** A run that loses track of dry matter is not a run.\n\n";
  }
}

void write_evidence_notes(std::ostringstream& out, const ScenarioBundle& bundle) {
  out << "## What this report may be relied on for\n\n";
  out << "The evidence under this model is uneven, and a report that did not say so would be "
         "worse than no report. The full working is in `docs/validation/verify.md`.\n\n";

  out << "| | |\n|---|---|\n";
  out << "| Conservation, deterministic replay | **Sound** — properties of the bookkeeping |\n";
  out << "| Comparisons between managements | **Sound** — both arms carry the same parameters, "
         "so an error in one cancels |\n";
  out << "| Cattle maintenance | **Sound to about 2%** against DairyNZ across 300–600 kg |\n";
  out << "| Sheep maintenance | **Low by about 15%** against CSIRO, and 21% against Nicol and "
         "Brookes. The basal term agrees to under 1%; the gap is the cost of grazing, walking "
         "and activity. CSIRO is the closer comparison because it accounts for chewing inside "
         "the same efficiency term this model does |\n";
  out << "| Carrying capacity for sheep | **Overstated by about 18%** — follows from the line "
         "above |\n";
  out << "| Absolute liveweight gain | **Not quotable** — standard reference weight is "
         "unverified on every species |\n";
  out << "| Anything involving a milking cow | **Not modelled** — lactation is absent |\n";
  out << "| Nitrogen over more than a season | **Wrong in a known direction** — dung and urine "
         "are not returned |\n";
  if (bundle.terrain.is_flat()) {
    out << "| Anything to do with slope | **Not in this run** — the ground was modelled flat, so "
           "neither the cost of walking a slope nor the radiation a slope receives applied |\n";
  } else {
    out << "| Where the hills are | **Invented** — the surface is a formula, chosen so the "
           "topography can be checked against known derivatives. It is not a survey of any "
           "ground |\n";
  }
  out << "\n";

  out << "The pasture, soil and weather definitions in the example scenarios are placeholders "
         "and are marked as such in their own files. They demonstrate that the model runs and "
         "settles; they do not describe any real farm.\n\n";
}

}  // namespace

std::string render_report(const ScenarioBundle& bundle, const RunSummary& run,
                          const ReportOptions& options) {
  const std::vector<Month> months = by_month(run);

  std::ostringstream out;
  write_heading(out, bundle, run, options);
  write_what_was_simulated(out, bundle, options);
  write_pasture(out, run, months);
  write_stock(out, run, months);
  // Both of these are about stock: what the farmer did with it and what it had
  // to be fed. With none on the farm they would report a farmer who made no
  // decisions and a feed bill of nothing, which reads as a finding rather than
  // as an absence. The stock section above says they have been left out.
  if (!run.liveweight_kg.empty()) {
    write_management(out, run);
    write_bought_feed(out, run);
  }
  write_budgets(out, run);
  if (options.include_evidence_notes) {
    write_evidence_notes(out, bundle);
  }
  return out.str();
}

std::string render_comparison_report(const ScenarioBundle& bundle, const RunSummary& left,
                                     const RunSummary& right, const ReportOptions& options) {
  std::ostringstream out;
  const std::string name = options.farm_name.empty() ? bundle.name : options.farm_name;

  out << "# " << name << " — " << left.label << " against " << right.label << "\n\n";
  out << "The same farm, the same weather, the same stock and the same stocking rate. Only the "
         "management differs, which is what lets the difference be attributed to it.\n\n";

  out << "| | " << left.label << " | " << right.label << " |\n|---|---|---|\n";
  out << "| Liveweight change | " << fixed(left.liveweight_change_kg(), 2) << " kg | "
      << fixed(right.liveweight_change_kg(), 2) << " kg |\n";
  out << "| Days the stock went short | " << left.feed_supply_short_days << " | " << right.feed_supply_short_days << " |\n";
  out << "| Mean cover | " << fixed(left.mean_cover_kg_dm_per_ha(), 0) << " | "
      << fixed(right.mean_cover_kg_dm_per_ha(), 0) << " kg DM/ha |\n";
  out << "| Lowest cover | " << fixed(left.lowest_cover_kg_dm_per_ha(), 0) << " | "
      << fixed(right.lowest_cover_kg_dm_per_ha(), 0) << " kg DM/ha |\n";
  out << "| Pasture eaten | " << fixed(left.eaten_kg_dm, 0) << " | " << fixed(right.eaten_kg_dm, 0)
      << " kg DM |\n";
  out << "| **Feed bought in** | **" << fixed(left.bought_feed_kg_dm(), 0) << " kg DM** | **"
      << fixed(right.bought_feed_kg_dm(), 0) << " kg DM** |\n";
  out << "| Days feed was bought | " << left.days_feed_was_bought() << " | "
      << right.days_feed_was_bought() << " |\n";
  out << "| Mob shifts | " << left.moves << " | " << right.moves << " |\n\n";

  const double feed_difference = right.bought_feed_kg_dm() - left.bought_feed_kg_dm();
  out << "**" << right.label << "** needed " << fixed(std::abs(feed_difference), 0) << " kg DM "
      << (feed_difference > 0.0 ? "more" : "less") << " bought feed than **" << left.label << "**";
  const double weight_difference = right.liveweight_change_kg() - left.liveweight_change_kg();
  out << ", and left the stock " << fixed(std::abs(weight_difference), 2) << " kg "
      << (weight_difference > 0.0 ? "heavier" : "lighter") << ".\n\n";

  out << "Read those two together. A management that saves feed by letting stock lose condition "
         "has not saved anything, and one that holds condition on bought feed has moved the cost "
         "rather than removed it.\n\n";

  if (options.include_evidence_notes) {
    write_evidence_notes(out, bundle);
  }
  return out.str();
}

}  // namespace paddock::config
