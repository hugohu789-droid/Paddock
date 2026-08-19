// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#pragma once

#include <cstdint>
#include <string>

namespace paddock::core {

/// Deciding whether to irrigate, and how much of what is asked for can be
/// delivered.
///
/// **Three things are kept apart here, and the separation is the design.**
///
///   * The soil says how dry it is. It does not decide anything, and nothing
///     in `SoilWaterBucket` calls into this file.
///   * `IrrigationPolicy` is a person's rule: at what dryness to start, how far
///     to refill, how much to put on at once, how often at most.
///   * `IrrigationSystem` is what the plant can actually do: how much of the
///     water put out reaches the root zone, and how deep an application it can
///     manage.
///
/// `decide_irrigation` reads the first, asks the second and clips to the third.
/// It returns a decision; it applies nothing. The caller puts the water on,
/// which keeps the water balance the one place water enters the model.
///
/// This is the same separation the rest of the project keeps between state,
/// biology, management and orchestration, and it is what will let a different
/// rule - a calendar, an allocation limit, an optimiser - be swapped in without
/// touching the soil.

/// A person's rule for when and how much to irrigate.
struct IrrigationPolicy {
  /// Whether to irrigate at all. False is the default so that a farm becomes
  /// irrigated only when somebody says so, and a rain-fed run is the thing you
  /// get without asking.
  bool enabled = false;

  /// Start when the root zone depletion reaches this share of the total
  /// available water.
  ///
  /// FAO-56's own trigger is the readily available water, RAW = p x TAW
  /// (Eq. 83), where p is the depletion fraction the crop can stand before it
  /// feels the shortage - 0.6 for grazed pasture in FAO-56 Table 22. Setting
  /// this to that p irrigates exactly at the point the pasture would start to
  /// be held back; setting it lower keeps the profile wetter and spends more
  /// water.
  double trigger_depletion_fraction = 0.5;

  /// Refill to this share of the total available water, measured as depletion.
  ///
  /// Zero would be field capacity, and irrigating to field capacity wastes the
  /// next rain: a full profile has nowhere to put it and it drains. Leaving a
  /// little room is standard practice and is what this default does.
  double target_depletion_fraction = 0.15;

  /// The most to put on in one go, mm. Zero means no limit from the rule -
  /// though the system may still have one.
  double maximum_application_mm = 25.0;

  /// The fewest days between irrigations of the same ground. Zero allows
  /// consecutive days.
  int minimum_return_days = 0;
};

/// What the irrigation plant can actually deliver.
struct IrrigationSystem {
  /// The share of the water put out that reaches the root zone, 0 to 1.
  ///
  /// **Defaults to 1.0, and that is deliberate.** A real spray or pivot loses
  /// water to wind drift, evaporation and uneven distribution, and the usual
  /// figures quoted sit somewhere in the eighties - but this project does not
  /// have a source for a New Zealand system, and a default of 0.85 would put a
  /// 15% loss into every run that nobody had chosen and nobody could cite. So
  /// the default is "no loss modelled", it is stated rather than hidden, and a
  /// scenario that knows its system's efficiency sets it.
  double application_efficiency = 1.0;

  /// The deepest single application the plant can manage, mm. Zero means the
  /// plant is not the limit.
  double maximum_application_mm = 0.0;

  [[nodiscard]] std::string validation_error() const;
};

/// What was decided for one paddock on one day.
struct IrrigationDecision {
  bool irrigate = false;

  /// What the rule wanted at the soil, before any limit, mm.
  double requested_mm = 0.0;

  /// What reaches the root zone after every limit, mm. **This is the number
  /// that goes into the water balance.**
  double effective_mm = 0.0;

  /// What has to be put out to deliver that, mm. Equal to `effective_mm` when
  /// the efficiency is 1.
  double applied_mm = 0.0;

  /// The water that has to be pumped for one hectare, cubic metres.
  ///
  /// One millimetre over one hectare is 10 m3, which is 10 tonnes. That is
  /// arithmetic rather than a model: 0.001 m x 10 000 m2.
  double pumped_m3_per_ha = 0.0;

  /// Why nothing was done, when nothing was done. Empty when it was.
  std::string held_back;
};

/// The water in one millimetre of application over one hectare, cubic metres.
inline constexpr double kCubicMetresPerMmPerHectare = 10.0;

/// Decides whether to irrigate, and how much.
///
/// `depletion_mm` and `total_available_water_mm` come from the soil as it
/// stands this morning - the soil reports, it does not decide.
/// `days_since_last` is how long since this ground was last watered; pass a
/// large number when it never has been.
///
/// Returns a decision with nothing applied. The caller puts `effective_mm`
/// into the water balance, which keeps the accounting in one place.
[[nodiscard]] IrrigationDecision decide_irrigation(double depletion_mm,
                                                   double total_available_water_mm,
                                                   int days_since_last,
                                                   const IrrigationPolicy& policy,
                                                   const IrrigationSystem& system);

/// What a season of irrigating came to.
///
/// Kept in the core rather than worked out where it is printed, so that two
/// ways of reporting a run cannot disagree about how much water it used.
struct IrrigationTally {
  int events = 0;
  double effective_mm = 0.0;
  double applied_mm = 0.0;
  double pumped_m3_per_ha = 0.0;

  void record(const IrrigationDecision& decision);

  /// The mean depth of an irrigation, mm, or zero when there were none.
  [[nodiscard]] double mean_event_mm() const noexcept;

  /// What was pumped over a farm of this many hectares, cubic metres.
  [[nodiscard]] double pumped_m3(double hectares) const noexcept;

  /// The same in megalitres, which is the unit a consent is written in.
  [[nodiscard]] double pumped_megalitres(double hectares) const noexcept;
};

}  // namespace paddock::core
