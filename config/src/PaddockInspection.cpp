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

  inspection.morning_water_fraction = paddock_mean(day.morning_water, mask, paddock);
  if (record.irrigation != nullptr) {
    inspection.irrigation_enabled = record.irrigation->enabled;
    // The policy is written in depletion - "start when half the water is gone" -
    // and the panel reads in what is left, which is how the setup form asks for
    // it. One subtraction, in one place, so the two cannot drift apart.
    inspection.irrigation_trigger_fraction = 1.0 - record.irrigation->trigger_depletion_fraction;
    inspection.irrigation_target_fraction = 1.0 - record.irrigation->target_depletion_fraction;
  }
  inspection.irrigation_held_back = record.held_back;

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

std::string irrigation_reason_phrase(const PaddockInspection& inspection) {
  // Off is said by the status beside it, so there is nothing for a phrase to
  // add.
  if (!inspection.irrigation_enabled) {
    return {};
  }

  // **The figures around this phrase are all decision-time.** The schedule
  // looks at the root zone before the day runs; what anyone sees tonight has
  // had the rain, the grass and the water itself. A day watered at 45% often
  // ends at 84%, and explaining it with the 84% would read as the farm watering
  // ground that was already wet.
  const double today = inspection.irrigation_today_mm.value_or(0.0);
  if (today > 0.0) {
    return inspection.morning_water_fraction.has_value() ? "at or below the trigger" : "";
  }

  // The run's own reason, when it recorded one. "The profile is still wetter
  // than the trigger" is the schedule talking, not this file guessing.
  return inspection.irrigation_held_back;
}

}  // namespace paddock::config
