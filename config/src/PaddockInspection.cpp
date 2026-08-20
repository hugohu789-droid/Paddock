// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#include <algorithm>
#include <sstream>
#include <string>

#include <paddock/config/PaddockInspection.hpp>

namespace paddock::config {

namespace {

/// A number as a report writes it rather than as a double prints it.
std::string fixed(double value, int places) {
  std::ostringstream out;
  out.setf(std::ios::fixed);
  out.precision(places);
  out << value;
  return out.str();
}

}  // namespace

std::optional<double> paddock_mean(const core::Raster<double>* raster,
                                   const core::PaddockMask& mask, std::size_t paddock) {
  if (raster == nullptr || raster->empty()) {
    return std::nullopt;
  }
  double total = 0.0;
  std::size_t counted = 0;
  for (std::size_t row = 0; row < raster->rows(); ++row) {
    for (std::size_t col = 0; col < raster->cols(); ++col) {
      if (mask.owner(col, row) == paddock) {
        total += (*raster)(col, row);
        ++counted;
      }
    }
  }
  if (counted == 0) {
    return std::nullopt;
  }
  return total / static_cast<double>(counted);
}

PaddockInspection inspect_paddock(std::size_t paddock, const std::string& name,
                                  const core::PaddockMask& mask, const PaddockDay& day,
                                  const PaddockDayRecord& record) {
  PaddockInspection inspection;
  inspection.index = paddock;
  // The survey's name when it has one. "Paddock 7" is a label rather than a
  // name, so it is built here and not stored as though the ground came with it.
  inspection.name = name.empty() ? "Paddock " + std::to_string(paddock + 1) : name;
  inspection.hectares = mask.rasterised_hectares(paddock);
  inspection.cells =
      paddock < mask.cell_counts().size() ? mask.cell_counts()[paddock] : std::size_t{0};

  inspection.cover_kg_dm_per_ha = paddock_mean(day.cover, mask, paddock);
  inspection.growth_kg_dm_per_ha = paddock_mean(day.growth, mask, paddock);
  inspection.available_water_fraction = paddock_mean(day.available_water, mask, paddock);
  inspection.water_growth_factor = paddock_mean(day.water_stress, mask, paddock);
  inspection.irrigation_today_mm = paddock_mean(day.irrigation_today, mask, paddock);
  inspection.irrigation_to_date_mm = paddock_mean(day.irrigation_to_date, mask, paddock);

  inspection.irrigation_enabled = record.irrigation_enabled;

  if (record.grazed != nullptr) {
    inspection.stock_today =
        std::find(record.grazed->begin(), record.grazed->end(), paddock) != record.grazed->end();
  }
  if (record.rest_days != nullptr && paddock < record.rest_days->size()) {
    inspection.rest_days = (*record.rest_days)[paddock];
  }
  if (record.policy != nullptr) {
    inspection.minimum_spell_days = record.policy->minimum_spell_days;
    inspection.maximum_graze_days = record.policy->maximum_graze_days;
  }
  return inspection;
}

std::string inspection_line(const PaddockInspection& inspection) {
  const auto figure = [](const std::optional<double>& value, int places,
                         const std::string& suffix) -> std::string {
    return value.has_value() ? fixed(*value, places) + suffix : std::string("-");
  };

  std::string line = inspection.name + " " + fixed(inspection.hectares, 1) + " ha, " +
                     std::to_string(inspection.cells) + " cells";
  line += " | cover " + figure(inspection.cover_kg_dm_per_ha, 0, " kg DM/ha");
  line += " | grew " + figure(inspection.growth_kg_dm_per_ha, 1, " kg DM/ha today");
  line += " | water left ";
  line += inspection.available_water_fraction.has_value()
              ? fixed(*inspection.available_water_fraction * 100.0, 0) + "%"
              : std::string("-");
  line += " | water growth factor " + figure(inspection.water_growth_factor, 2, "");
  if (inspection.irrigation_enabled) {
    line += " | irrigation " + figure(inspection.irrigation_today_mm, 1, " mm") + " today, " +
            figure(inspection.irrigation_to_date_mm, 0, " mm") + " so far";
  } else {
    line += " | irrigation off";
  }
  line += inspection.stock_today ? " | stock on it" : " | no stock";
  if (inspection.rest_days.has_value()) {
    line += " | rested " + std::to_string(*inspection.rest_days) + " days";
  }
  return line;
}

std::string grazing_rule_sentence(const core::ManagementPolicy& policy) {
  return "A mob moves when it has gone short or after " +
         std::to_string(policy.maximum_graze_days) +
         " days on its paddock, and goes to whichever free paddock has rested longest. The " +
         std::to_string(policy.minimum_spell_days) +
         "-day spell is what the farmer aims at, not a gate: a mob moves onto ground that has "
         "had less, and the run records that it happened.";
}

std::string irrigation_sentence(const PaddockInspection& inspection) {
  if (!inspection.irrigation_enabled) {
    return "Irrigation is off in this scenario.";
  }
  const double today = inspection.irrigation_today_mm.value_or(0.0);
  if (today > 0.0) {
    // The rule is exactly this - the schedule waters ground whose depletion has
    // passed the trigger - so saying it is reporting the rule, not guessing at
    // one.
    return "Watered today: the soil had dried past the trigger.";
  }
  if (!inspection.available_water_fraction.has_value()) {
    return "Not watered today.";
  }
  // **What the soil holds now, which is not what the schedule read.** The
  // schedule decides in the morning, on the depletion as it stood then, and
  // what is recorded here is the day's end. Saying the trigger was or was not
  // met would be reading a decision off the wrong number, so this says what the
  // figure is and leaves the reason to the run that made it.
  return "Not watered today. Soil ended the day at " +
         fixed(*inspection.available_water_fraction * 100.0, 0) + "% of capacity.";
}

}  // namespace paddock::config
