// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

// Deciding whether to irrigate, and how much of what is asked for arrives.
//
// The separation this file is really testing is that the decision is made in
// one place and applied in another: nothing here touches a soil profile, and
// SoilWaterBucket never asks anything here what to do. What is checked is the
// rule, the plant's limits, and the arithmetic between millimetres and cubic
// metres - which is where a farm's water bill comes from.

#include <gtest/gtest.h>

#include <paddock/core/Irrigation.hpp>

namespace paddock::core {
namespace {

/// A soil holding 120 mm of available water, which is a moderately deep silt
/// loam under pasture.
constexpr double kTaw = 120.0;

IrrigationPolicy watering_policy() {
  IrrigationPolicy policy;
  policy.enabled = true;
  policy.trigger_depletion_fraction = 0.5;
  policy.target_depletion_fraction = 0.15;
  policy.maximum_application_mm = 25.0;
  policy.minimum_return_days = 0;
  return policy;
}

TEST(IrrigationTest, OffMeansOff) {
  IrrigationPolicy policy = watering_policy();
  policy.enabled = false;

  // Bone dry, and still nothing happens.
  const IrrigationDecision decision = decide_irrigation(kTaw, kTaw, 99, policy, {});

  EXPECT_FALSE(decision.irrigate);
  EXPECT_DOUBLE_EQ(decision.effective_mm, 0.0);
  EXPECT_DOUBLE_EQ(decision.pumped_m3_per_ha, 0.0);
  EXPECT_FALSE(decision.held_back.empty()) << "a run that did nothing should say why";
}

// The trigger is a threshold, not a slope: above it nothing, at it everything.
TEST(IrrigationTest, NothingHappensUntilTheProfileReachesTheTrigger) {
  const IrrigationPolicy policy = watering_policy();

  // 0.5 x 120 = 60 mm of depletion is the trigger.
  EXPECT_FALSE(decide_irrigation(59.0, kTaw, 99, policy, {}).irrigate);
  EXPECT_TRUE(decide_irrigation(60.0, kTaw, 99, policy, {}).irrigate);
  EXPECT_TRUE(decide_irrigation(61.0, kTaw, 99, policy, {}).irrigate);
}

// **Refilling stops short of field capacity on purpose.** A full profile has
// nowhere to put the next rain, which then drains - water bought and lost,
// with whatever nitrogen goes down with it.
TEST(IrrigationTest, RefillsToTheTargetRatherThanToFieldCapacity) {
  IrrigationPolicy policy = watering_policy();
  policy.maximum_application_mm = 0.0;  // No cap, so the target is what shows.

  // 80 mm down, target 0.15 x 120 = 18 mm down: ask for 62 mm.
  const IrrigationDecision decision = decide_irrigation(80.0, kTaw, 99, policy, {});

  EXPECT_TRUE(decision.irrigate);
  EXPECT_DOUBLE_EQ(decision.requested_mm, 62.0);
  EXPECT_DOUBLE_EQ(decision.effective_mm, 62.0);
  EXPECT_LT(decision.effective_mm, 80.0) << "refilling all the way is what the target prevents";
}

TEST(IrrigationTest, ThePolicysCapLimitsTheEvent) {
  const IrrigationPolicy policy = watering_policy();  // 25 mm cap.

  const IrrigationDecision decision = decide_irrigation(100.0, kTaw, 99, policy, {});

  EXPECT_DOUBLE_EQ(decision.requested_mm, 82.0) << "what the profile wanted";
  EXPECT_DOUBLE_EQ(decision.effective_mm, 25.0) << "what the rule allowed";
}

// The rule and the plant are separate limits, and either can be the binding
// one. Both use zero for "not a limit", which is why this is not a plain
// minimum over the two.
TEST(IrrigationTest, ThePlantsCapLimitsTheEventToo) {
  IrrigationPolicy policy = watering_policy();
  policy.maximum_application_mm = 0.0;

  IrrigationSystem system;
  system.maximum_application_mm = 12.0;

  const IrrigationDecision decision = decide_irrigation(100.0, kTaw, 99, policy, system);
  EXPECT_DOUBLE_EQ(decision.effective_mm, 12.0);

  // And with no limit anywhere, the whole request goes on.
  const IrrigationDecision unlimited = decide_irrigation(100.0, kTaw, 99, policy, {});
  EXPECT_DOUBLE_EQ(unlimited.effective_mm, 82.0);
}

// **Efficiency divides.** The caps are on what the ground gets; the pump has
// to put out more than that to deliver it. Multiplying instead would
// under-water the paddock by the efficiency and report the shortfall as the
// plan.
TEST(IrrigationTest, EfficiencyIsChargedToThePumpAndNotToTheGround) {
  const IrrigationPolicy policy = watering_policy();

  IrrigationSystem leaky;
  leaky.application_efficiency = 0.8;

  const IrrigationDecision decision = decide_irrigation(100.0, kTaw, 99, policy, leaky);

  EXPECT_DOUBLE_EQ(decision.effective_mm, 25.0) << "the ground still gets what was asked for";
  EXPECT_DOUBLE_EQ(decision.applied_mm, 31.25) << "and the pump puts out more to deliver it";
  EXPECT_GT(decision.applied_mm, decision.effective_mm);
}

// One millimetre over one hectare is ten cubic metres. It is arithmetic -
// 0.001 m x 10 000 m2 - and it is where a water bill comes from, so it is
// pinned.
TEST(IrrigationTest, OneMillimetreOverOneHectareIsTenCubicMetres) {
  IrrigationPolicy policy = watering_policy();
  policy.maximum_application_mm = 25.0;

  const IrrigationDecision decision = decide_irrigation(100.0, kTaw, 99, policy, {});

  EXPECT_DOUBLE_EQ(decision.applied_mm, 25.0);
  EXPECT_DOUBLE_EQ(decision.pumped_m3_per_ha, 250.0);

  IrrigationTally tally;
  tally.record(decision);
  EXPECT_DOUBLE_EQ(tally.pumped_m3(100.0), 25000.0) << "25 mm over 100 ha";
  EXPECT_DOUBLE_EQ(tally.pumped_megalitres(100.0), 25.0);
}

TEST(IrrigationTest, TheReturnIntervalHoldsTheNextWatering) {
  IrrigationPolicy policy = watering_policy();
  policy.minimum_return_days = 5;

  EXPECT_FALSE(decide_irrigation(100.0, kTaw, 4, policy, {}).irrigate);
  EXPECT_TRUE(decide_irrigation(100.0, kTaw, 5, policy, {}).irrigate);
}

// A season's totals, kept in the core so two reports of one run cannot
// disagree about how much water it used.
TEST(IrrigationTest, ASeasonAddsUp) {
  const IrrigationPolicy policy = watering_policy();
  IrrigationSystem system;
  system.application_efficiency = 0.8;

  IrrigationTally tally;
  for (int event = 0; event < 4; ++event) {
    tally.record(decide_irrigation(100.0, kTaw, 99, policy, system));
  }
  // And one day that did nothing, which must not count as an event.
  tally.record(decide_irrigation(10.0, kTaw, 99, policy, system));

  EXPECT_EQ(tally.events, 4);
  EXPECT_DOUBLE_EQ(tally.effective_mm, 100.0);
  EXPECT_DOUBLE_EQ(tally.applied_mm, 125.0);
  EXPECT_DOUBLE_EQ(tally.mean_event_mm(), 25.0);
  EXPECT_DOUBLE_EQ(tally.pumped_m3(80.0), 100000.0) << "125 mm over 80 ha";
  EXPECT_DOUBLE_EQ(tally.pumped_megalitres(80.0), 100.0);
}

TEST(IrrigationTest, AnImpossibleSystemIsRefused) {
  IrrigationSystem system;
  EXPECT_TRUE(system.validation_error().empty());

  system.application_efficiency = 0.0;
  EXPECT_FALSE(system.validation_error().empty());

  system.application_efficiency = 1.2;
  EXPECT_FALSE(system.validation_error().empty()) << "more water arrives than was put out";

  system.application_efficiency = 0.9;
  system.maximum_application_mm = -1.0;
  EXPECT_FALSE(system.validation_error().empty());
}

// A soil that holds no water cannot be refilled, and asking is not an error.
TEST(IrrigationTest, ASoilThatHoldsNothingIsNotWatered) {
  const IrrigationDecision decision = decide_irrigation(0.0, 0.0, 99, watering_policy(), {});
  EXPECT_FALSE(decision.irrigate);
  EXPECT_FALSE(decision.held_back.empty());
}

}  // namespace
}  // namespace paddock::core
