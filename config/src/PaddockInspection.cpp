// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#include <algorithm>
#include <cmath>
#include <sstream>
#include <string>
#include <vector>

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

/// A percentage of a fraction, or a dash where the run kept none.
std::string share(const std::optional<double>& fraction) {
  return fraction.has_value() ? fixed(*fraction * 100.0, 0) + "%" : std::string("-");
}

std::string amount(const std::optional<double>& value, int places, const std::string& unit) {
  return value.has_value() ? fixed(*value, places) + unit : std::string("-");
}

}  // namespace

PaddockInspection::IrrigationStatus PaddockInspection::irrigation_status() const {
  if (!irrigation_enabled) {
    return IrrigationStatus::Off;
  }
  // Water that actually went on outranks a held-back reason. On a farm where
  // some cells were watered and others were not, the schedule records the first
  // cell's reason for the whole day - so a paddock that got water and a reason
  // both got water, and saying otherwise would be reading another paddock's
  // day.
  if (irrigation_today_mm.value_or(0.0) > 0.0) {
    return IrrigationStatus::Watered;
  }
  if (!irrigation_held_back.empty()) {
    return IrrigationStatus::HeldBack;
  }
  return IrrigationStatus::NotRecorded;
}

std::string PaddockInspection::irrigation_status_text() const {
  switch (irrigation_status()) {
    case IrrigationStatus::Off:
      return "off in this scenario";
    case IrrigationStatus::Watered:
      return "watered";
    case IrrigationStatus::HeldBack:
      return "held back";
    case IrrigationStatus::NotRecorded:
      break;
  }
  return "not recorded";
}

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
  line += " | water left " + share(inspection.available_water_fraction);
  line += " | water growth factor " + figure(inspection.water_growth_factor, 2, "");
  // **The status, not only the depth.** "0.0 mm today" is a farm that was not
  // due, a farm that was watered two days ago, and a farm with no irrigation at
  // all, and the console line could not tell them apart any more than the panel
  // could.
  line += " | irrigation " + inspection.irrigation_status_text();
  if (inspection.irrigation_status() != PaddockInspection::IrrigationStatus::Off) {
    // **The reading the decision was made on, beside the decision.** Without it
    // this line says a farm put 25 mm on ground that is at 83%, which is the
    // one sentence about irrigation nobody should be allowed to take away from
    // this model. It was at 38% when the schedule looked; the 83% is partly the
    // water itself.
    if (inspection.morning_water_fraction.has_value()) {
      line += " (decided at " + share(inspection.morning_water_fraction) + ")";
    }
    line += ", " + figure(inspection.irrigation_today_mm, 1, " mm") + " today, " +
            figure(inspection.irrigation_to_date_mm, 0, " mm") + " so far";
  }
  line += inspection.stock_today ? " | stock on it" : " | no stock";
  if (inspection.rest_days.has_value()) {
    line += " | rested " + std::to_string(*inspection.rest_days) + " days";
  }
  return line;
}

InspectorPanel inspector_panel(const PaddockInspection& inspection,
                               const std::string& grazing_rule) {
  InspectorPanel panel;
  panel.heading = inspection.name;
  panel.subheading =
      fixed(inspection.hectares, 1) + " ha, " + std::to_string(inspection.cells) + " cells";
  panel.grazing_rule = grazing_rule;

  // ---------------------------------------------------------------- pasture
  PanelSection pasture;
  pasture.title = "Pasture";
  pasture.rows.push_back(
      {"Cover", amount(inspection.cover_kg_dm_per_ha, 0, " kg DM/ha"), std::string{}});
  pasture.rows.push_back(
      {"Grew today", amount(inspection.growth_kg_dm_per_ha, 1, " kg DM/ha"), std::string{}});
  panel.sections.push_back(std::move(pasture));

  // ------------------------------------------------------------------ water
  //
  // **The subtitle is the whole fix for the question this panel used to
  // invite.** A reader who sees 84% here and a decision to irrigate below has
  // to be told, in the heading rather than in a footnote, that these two
  // readings are the same soil at different ends of the day.
  PanelSection water;
  water.title = "Water tonight";
  water.subtitle = "after today's rain, growth and any irrigation";
  water.rows.push_back({"Available water", share(inspection.available_water_fraction),
                        "of what this soil can hold"});
  // Named for what it does, and told what its own scale means. "Stress 1.00"
  // reads as the worst day of the year to anybody who has not been told.
  water.rows.push_back({"Growth factor", amount(inspection.water_growth_factor, 2, ""),
                        "1.00 is unrestricted; below it the model held growth back"});
  panel.sections.push_back(std::move(water));

  // ------------------------------------------------------------- irrigation
  PanelSection irrigation;
  irrigation.title = "Irrigation";
  irrigation.rows.push_back({"Decision", inspection.irrigation_status_text(), std::string{}});

  if (inspection.irrigation_status() != PaddockInspection::IrrigationStatus::Off) {
    irrigation.subtitle = "decided this morning, before any of the above";

    // **Shown whatever was decided, which it was not before.** The morning
    // reading was inside the branch that only ran when water went on, so a dry
    // day showed the trigger and hid the number it was tested against - and a
    // dry day is exactly when somebody asks why nothing happened.
    irrigation.rows.push_back({"Soil water then", share(inspection.morning_water_fraction),
                               "the reading the trigger was tested against"});
    irrigation.rows.push_back({"Waters at or below",
                               fixed(inspection.irrigation_trigger_fraction * 100.0, 0) + "%",
                               std::string{}});

    const std::string why = irrigation_reason_phrase(inspection);
    if (!why.empty()) {
      irrigation.rows.push_back({"Because", why, std::string{}});
    }

    irrigation.rows.push_back(
        {"Applied today", amount(inspection.irrigation_today_mm, 1, " mm"), std::string{}});

    // **The refill target only where it is relevant.** It is where the water
    // was aiming, so on a day nothing was applied it is a setting rather than
    // an explanation, and a panel this short cannot spend a line on it.
    if (inspection.irrigation_status() == PaddockInspection::IrrigationStatus::Watered) {
      irrigation.rows.push_back({"Refilling toward",
                                 fixed(inspection.irrigation_target_fraction * 100.0, 0) + "%",
                                 std::string{}});
    }
    irrigation.rows.push_back(
        {"Season to date", amount(inspection.irrigation_to_date_mm, 0, " mm"), std::string{}});
  }
  panel.sections.push_back(std::move(irrigation));

  // ---------------------------------------------------------------- grazing
  //
  // **Only what the farm recorded.** There is no per-paddock gate in this model
  // - a mob moves when it has gone short or has been somewhere long enough, and
  // goes to whichever free paddock has rested longest - so there is nothing here
  // that says whether this paddock was eligible, because nothing decides that.
  PanelSection grazing;
  grazing.title = "Grazing";
  grazing.rows.push_back({"Stock today", inspection.stock_today ? "on it" : "none", std::string{}});
  if (inspection.rest_days.has_value()) {
    grazing.rows.push_back(
        {"Rested", std::to_string(*inspection.rest_days) + " days", std::string{}});
  }
  if (inspection.minimum_spell_days > 0) {
    // "Aims at" rather than "minimum", because it is not one. A mob moves onto
    // ground that has had less rest than this whenever nothing else is free,
    // and grazing_rule_sentence says so at length.
    grazing.rows.push_back({"Farmer aims at",
                            std::to_string(inspection.minimum_spell_days) + " days rest",
                            "a target, not a gate: a mob moves onto ground that has had less"});
  }
  if (inspection.maximum_graze_days > 0) {
    grazing.rows.push_back({"Moves after",
                            std::to_string(inspection.maximum_graze_days) + " days on a paddock",
                            std::string{}});
  }
  panel.sections.push_back(std::move(grazing));

  return panel;
}

bool selection_survives(const std::vector<core::Paddock>& before,
                        const std::vector<core::Paddock>& after, std::size_t selected) {
  if (before.size() != after.size() || selected >= after.size()) {
    return false;
  }
  // Name and area, not the whole boundary. Comparing every vertex would drop a
  // selection because a raster was rebuilt at a different resolution, and the
  // question here is whether this is the same field - not whether the two
  // polygons are bit-identical.
  constexpr double kSquareMetre = 1e-4;  // hectares
  return before[selected].name == after[selected].name &&
         std::abs(before[selected].area_hectares() - after[selected].area_hectares()) <
             kSquareMetre;
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
  if (inspection.irrigation_status() == PaddockInspection::IrrigationStatus::Off) {
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
