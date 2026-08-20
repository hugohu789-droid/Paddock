// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include <paddock/core/Farmer.hpp>
#include <paddock/core/Irrigation.hpp>
#include <paddock/core/PaddockMask.hpp>
#include <paddock/core/Raster.hpp>

namespace paddock::config {

/// One paddock, on one day, as a person reads it.
///
/// **Facts the run recorded, and nothing worked out here.** Every field is
/// either read straight off the day's rasters, off the farm's own bookkeeping,
/// or off the management policy the run was given. Nothing in this file decides
/// whether a paddock should have been irrigated or grazed: the model decides,
/// the run records, and this reports.
///
/// The optionals are the honest ones. A run that kept no growth raster has no
/// growth to show, and an empty figure says so where a zero would claim the
/// grass stopped.
struct PaddockInspection {
  std::size_t index = 0;
  /// The paddock's own name, or "Paddock 7" when the survey gave it none.
  std::string name;
  double hectares = 0.0;
  std::size_t cells = 0;

  std::optional<double> cover_kg_dm_per_ha;
  std::optional<double> growth_kg_dm_per_ha;

  std::optional<double> available_water_fraction;

  /// FAO-56's water stress coefficient: **one is unstressed**, and anything
  /// below it is the model saying growth was held back that day. Named for what
  /// the number does rather than for the shortage it measures, because "stress
  /// 1.00" reads as the opposite of what it means.
  std::optional<double> water_growth_factor;

  std::optional<double> irrigation_today_mm;
  std::optional<double> irrigation_to_date_mm;
  bool irrigation_enabled = false;

  /// How full the root zone was when the schedule read it that morning, which
  /// is the figure the decision was made on. Empty when the run kept none.
  std::optional<double> morning_water_fraction;

  /// The trigger and refill target the schedule was working to, as shares of
  /// what the soil can hold.
  double irrigation_trigger_fraction = 0.0;
  double irrigation_target_fraction = 0.0;

  /// Why the schedule held water back, in its own words, or empty when it held
  /// none back. Recorded by the run rather than worked out afterwards.
  std::string irrigation_held_back;

  bool stock_today = false;

  /// The rest this paddock had been given, as the farm counted it. Empty when
  /// the run kept no count - not when the paddock has never been grazed, which
  /// the farm reports as a large number of days.
  std::optional<int> rest_days;

  /// What the farmer was working to, so the rest above can be read against
  /// something. Zero when the run named no policy.
  int minimum_spell_days = 0;
  int maximum_graze_days = 0;
};

/// The day's rasters, as pointers because the caller owns them and they are
/// large. Any of them may be null: a run that did not keep a series leaves the
/// matching figure empty rather than reporting a zero.
struct PaddockDay {
  const core::Raster<double>* cover = nullptr;
  const core::Raster<double>* growth = nullptr;
  const core::Raster<double>* available_water = nullptr;
  const core::Raster<double>* water_stress = nullptr;
  const core::Raster<double>* irrigation_today = nullptr;
  const core::Raster<double>* irrigation_to_date = nullptr;
  /// The soil as the schedule read it that morning, before the day ran.
  const core::Raster<double>* morning_water = nullptr;
};

/// What the farm and its management said about this paddock today.
struct PaddockDayRecord {
  /// Paddocks carrying stock today, as the farm listed them.
  const std::vector<std::size_t>* grazed = nullptr;
  /// Days since each paddock was last grazed, as the farm counted them.
  const std::vector<int>* rest_days = nullptr;
  const core::ManagementPolicy* policy = nullptr;
  /// The rule the schedule was working to. Null for a run that was never given
  /// one, which reads as irrigation being off.
  const core::IrrigationPolicy* irrigation = nullptr;
  /// Why the schedule held water back that day, in its own words.
  std::string held_back;
};

/// The mean of `raster` over the cells `mask` gives to `paddock`, or empty when
/// the paddock owns no cells or the raster is missing.
[[nodiscard]] std::optional<double> paddock_mean(const core::Raster<double>* raster,
                                                 const core::PaddockMask& mask,
                                                 std::size_t paddock);

/// Gathers one paddock's day. `name` is the survey's name for it, which may be
/// empty; the caller has the paddock list and this does not.
[[nodiscard]] PaddockInspection inspect_paddock(std::size_t paddock, const std::string& name,
                                                const core::PaddockMask& mask,
                                                const PaddockDay& day,
                                                const PaddockDayRecord& record);

/// How the farmer moves stock, in one sentence, from the policy that was run.
///
/// **The rule as it is, not as a reader might assume.** This farm moves a mob
/// when it is hungry or has been on its paddock long enough, and sends it to
/// the free paddock that has rested longest - there is no per-paddock cover
/// threshold and no rest requirement that holds a mob back. Saying so is what
/// keeps the inspector from being read as "this paddock was refused".
[[nodiscard]] std::string grazing_rule_sentence(const core::ManagementPolicy& policy);

/// The same inspection as one line of plain text, for a console.
///
/// **One source for the panel and the printout.** The headless `--inspect`
/// check used to read the window's own label and strip the markup out of it,
/// which made the panel's wording part of a command line contract. Both now
/// render the same struct, so they cannot describe the same paddock
/// differently.
[[nodiscard]] std::string inspection_line(const PaddockInspection& inspection);

/// The step between what the soil was this morning and what was done about it:
/// why the schedule acted, or why it did not.
///
/// **Read as one line of a chain.** The panel puts it between the morning
/// figure and the water that went on, so it is a phrase rather than a sentence:
/// "at or below the trigger", "the profile is still wetter than the trigger".
/// The second of those is the schedule's own wording, recorded by the run -
/// nothing here works out why water was held back, because the run already
/// knows and a second opinion could disagree with it.
///
/// Empty when there is nothing to say.
[[nodiscard]] std::string irrigation_reason_phrase(const PaddockInspection& inspection);

}  // namespace paddock::config
