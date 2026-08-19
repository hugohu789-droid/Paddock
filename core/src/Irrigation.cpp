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

}  // namespace paddock::core
