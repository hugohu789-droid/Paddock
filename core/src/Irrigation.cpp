// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#include <algorithm>

#include <paddock/core/Irrigation.hpp>

namespace paddock::core {

std::string IrrigationSystem::validation_error() const {
  if (application_efficiency <= 0.0 || application_efficiency > 1.0) {
    return "application_efficiency must be in (0, 1]";
  }
  if (maximum_application_mm < 0.0) {
    return "maximum_application_mm cannot be negative";
  }
  return {};
}

IrrigationDecision decide_irrigation(double depletion_mm, double total_available_water_mm,
                                     int days_since_last, const IrrigationPolicy& policy,
                                     const IrrigationSystem& system) {
  IrrigationDecision decision;

  if (!policy.enabled) {
    decision.held_back = "irrigation is off";
    return decision;
  }
  if (total_available_water_mm <= 0.0) {
    decision.held_back = "the soil holds no available water to refill";
    return decision;
  }
  if (days_since_last < policy.minimum_return_days) {
    decision.held_back = "watered too recently";
    return decision;
  }

  // The trigger, on the profile as it stands this morning. FAO-56 Ch. 8: water
  // when the root zone depletion reaches the readily available water.
  const double trigger_mm = policy.trigger_depletion_fraction * total_available_water_mm;
  if (depletion_mm < trigger_mm) {
    decision.held_back = "the profile is still wetter than the trigger";
    return decision;
  }

  // How much to put back: enough to leave the profile at the target depletion.
  //
  // Refilling to field capacity is not the goal and the target says so. A full
  // profile has nowhere to put the next rain, which then drains - and drainage
  // is water bought and lost, plus whatever nitrogen goes with it.
  const double target_mm = policy.target_depletion_fraction * total_available_water_mm;
  decision.requested_mm = std::max(0.0, depletion_mm - target_mm);
  if (decision.requested_mm <= 0.0) {
    decision.held_back = "the profile is already at the target";
    return decision;
  }

  // What the rule and the plant will each allow. Zero means "not a limit",
  // which is why this cannot be a plain std::min over both.
  double allowed_mm = decision.requested_mm;
  if (policy.maximum_application_mm > 0.0) {
    allowed_mm = std::min(allowed_mm, policy.maximum_application_mm);
  }
  if (system.maximum_application_mm > 0.0) {
    allowed_mm = std::min(allowed_mm, system.maximum_application_mm);
  }

  decision.effective_mm = allowed_mm;

  // **Efficiency divides rather than multiplies here.** The limits above are
  // on what the ground gets; the pump has to put out more than that to deliver
  // it. Multiplying instead would quietly under-water the paddock by the
  // efficiency and then report the shortfall as though it were the plan.
  const double efficiency = std::clamp(system.application_efficiency, 0.001, 1.0);
  decision.applied_mm = decision.effective_mm / efficiency;
  decision.pumped_m3_per_ha = decision.applied_mm * kCubicMetresPerMmPerHectare;
  decision.irrigate = decision.effective_mm > 0.0;
  return decision;
}

void IrrigationTally::record(const IrrigationDecision& decision) {
  if (!decision.irrigate) {
    return;
  }
  ++events;
  effective_mm += decision.effective_mm;
  applied_mm += decision.applied_mm;
  pumped_m3_per_ha += decision.pumped_m3_per_ha;
}

double IrrigationTally::mean_event_mm() const noexcept {
  return events > 0 ? effective_mm / static_cast<double>(events) : 0.0;
}

double IrrigationTally::pumped_m3(double hectares) const noexcept {
  return pumped_m3_per_ha * std::max(0.0, hectares);
}

double IrrigationTally::pumped_megalitres(double hectares) const noexcept {
  // A megalitre is a thousand cubic metres.
  return pumped_m3(hectares) / 1000.0;
}

IrrigationSchedule::IrrigationSchedule(IrrigationPolicy policy, IrrigationSystem system,
                                       std::size_t cells)
    : policy_(policy),
      system_(system),
      // Never watered, so the return interval does not hold the first day back.
      days_since_last_(cells, 9999),
      applied_mm_(cells, 0.0) {}

const std::vector<double>& IrrigationSchedule::decide(const std::vector<double>& depletion_mm,
                                                      double total_available_water_mm) {
  applied_mm_.assign(days_since_last_.size(), 0.0);
  last_available_fraction_.assign(days_since_last_.size(), 0.0);
  last_held_back_.clear();
  last_mean_mm_ = 0.0;
  last_cells_watered_ = 0;
  if (days_since_last_.empty()) {
    return applied_mm_;
  }

  double total_effective_mm = 0.0;
  double total_applied_mm = 0.0;
  double total_pumped_m3_per_ha = 0.0;

  for (std::size_t cell = 0; cell < days_since_last_.size(); ++cell) {
    const double dry = cell < depletion_mm.size() ? depletion_mm[cell] : 0.0;
    // Kept before anything is decided or applied: this is the soil the
    // schedule is deciding on, which is not the soil anyone will see tonight.
    last_available_fraction_[cell] =
        total_available_water_mm > 0.0
            ? std::clamp(1.0 - (dry / total_available_water_mm), 0.0, 1.0)
            : 0.0;
    const IrrigationDecision decision =
        decide_irrigation(dry, total_available_water_mm, days_since_last_[cell], policy_, system_);
    if (last_held_back_.empty() && !decision.held_back.empty()) {
      last_held_back_ = decision.held_back;
    }
    if (decision.irrigate) {
      applied_mm_[cell] = decision.effective_mm;
      days_since_last_[cell] = 0;
      ++last_cells_watered_;
      total_effective_mm += decision.effective_mm;
      total_applied_mm += decision.applied_mm;
      total_pumped_m3_per_ha += decision.pumped_m3_per_ha;
    } else {
      days_since_last_[cell] += 1;
    }
  }

  // **The tally is a mean over the farm, not a sum over the cells.** Every
  // depth here is millimetres on one hectare of ground, so adding them across
  // a hundred cells would count the same millimetre a hundred times. A farm
  // that watered half its cells by 25 mm put on 12.5 mm.
  const auto cells = static_cast<double>(days_since_last_.size());
  last_mean_mm_ = total_effective_mm / cells;

  if (last_cells_watered_ > 0) {
    IrrigationDecision farm_day;
    farm_day.irrigate = true;
    farm_day.effective_mm = last_mean_mm_;
    farm_day.applied_mm = total_applied_mm / cells;
    farm_day.pumped_m3_per_ha = total_pumped_m3_per_ha / cells;
    tally_.record(farm_day);
  }
  return applied_mm_;
}

}  // namespace paddock::core
