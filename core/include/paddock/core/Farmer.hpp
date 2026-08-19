// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <paddock/core/Farm.hpp>
#include <paddock/core/GrazingCalendar.hpp>

/// Who decides where the stock go.
///
/// The farm knows how to move a mob; the calendar knows which system is meant
/// to be running today. Neither knows when to act, and this is the piece that
/// does - the last of the two gaps in the simulation loop.
namespace paddock::core {

/// One mob moved, or not moved when it should have been.
struct MobMove {
  std::size_t mob = 0;

  /// True when the mob was moved because it ran out of feed rather than because
  /// its graze length was up. On a farm with enough paddocks this is rare; on
  /// one without, it is most of them.
  bool moved_early = false;

  std::size_t from = 0;
  std::size_t to = 0;

  /// How long the mob had been on the paddock it left.
  int days_grazed = 0;

  /// How long the paddock it arrived on had been resting.
  int rest_days = 0;

  /// **The shuffle, made visible.** True when the best paddock available had
  /// not had the spell the rule asks for, and the mob went on anyway.
  ///
  /// Smith and Dawson (1976) define the shuffle as what rotational intent
  /// becomes when there are too few paddocks for the mobs: "the effect must be
  /// to lengthen the grazing period or shorten the spelling period". This flag
  /// is the second of those, and `Farmer::Day::grazings_extended` is the first.
  /// Neither is implemented as a mode - both fall out of a farm that cannot
  /// keep its own rules.
  bool spell_was_short = false;
};

/// How the farmer decides which grazing system to run.
///
/// The default is what this model did before there was a choice, and it is a
/// real decision rather than an absence of one: rotation concentrates stock
/// onto one paddock and only works if that paddock can carry them, so a farm
/// short of cover spreads out instead.
enum class GrazingPreference : std::uint8_t {
  /// Rotate when the mean cover can afford it, set stock when it cannot.
  ByCover,

  /// Rotate wherever possible, falling back to set stocking only at the cover
  /// floor. A farmer who believes in the rotation and will hold to it while the
  /// sward can stand it.
  PreferRotation,

  /// Never rotate. Smith and Dawson (1976) name set stocking for lambing, when
  /// demand is highest and the whole farm is wanted at once; a farm may be run
  /// that way all year, and the model should be able to show what that costs.
  AlwaysSetStock,

  /// Do what the bundle's own calendar says.
  ///
  /// **This was previously unreachable.** A scenario could carry
  /// `[[grazing_period]]` describing set stocking over lambing and rotation the
  /// rest of the year, and a managed run ignored every word of it, because the
  /// farmer decided from cover alone. The calendar was written down and nothing
  /// read it.
  FollowCalendar,
};

/// What the farmer buys once the sward is at the floor.
enum class FloorPurchase : std::uint8_t {
  /// Buy the mob's whole demand, so the pasture is asked for nothing and
  /// recovers. This is what the model did before there was a choice.
  WholeDemand,

  /// Let the stock graze down to the floor and buy the rest, so cover holds
  /// there rather than climbing away from it.
  ///
  /// **Not "buy whatever today's growth does not cover".** The farmer decides
  /// before the day is stepped, so today's growth is not yet known, and a rule
  /// written against it would be guessing at a number the model already has -
  /// tomorrow. Grazing down to the line is the same intent expressed in what is
  /// knowable when the decision is made.
  HoldAtFloor,
};

/// What a farmer is trying to do, and what they will not let happen.
///
/// A calendar says what the management is meant to be. A policy says what the
/// farmer does when the season does not cooperate - which is most of the time,
/// and is the part a fixed calendar cannot express.
struct ManagementPolicy {
  /// The farm's mean cover is not to be grazed below this. Below it the farmer
  /// buys feed instead of asking the pasture for more, because a sward taken
  /// too low stops growing and takes months to come back.
  ///
  /// PLACEHOLDER. New Zealand extension material talks about residuals in
  /// terms a farmer can see - a sward height, a cover after grazing - and this
  /// project has no sourced figure for the level at which a farmer should start
  /// feeding out. See docs/verify.md.
  double minimum_cover_kg_dm_per_ha = 1600.0;

  /// What the stock are meant to be doing, kg per head per day. **Stock are
  /// sold by the kilogram**, so holding weight is the floor rather than the
  /// goal: a farmer feeding to zero gain is feeding to lose money slowly.
  double target_liveweight_gain_kg_per_day = 0.0;

  /// Rotation parameters, when the farmer chooses to rotate.
  int maximum_graze_days = 3;
  int minimum_spell_days = 35;

  /// Cover at or above which rotating is affordable.
  ///
  /// Rotation concentrates stock onto one paddock, which only works if that
  /// paddock can carry them. When the farm is short, spreading out is what a
  /// farmer does - which is set stocking, and is why the source calls it the
  /// system for lambing when demand is highest.
  double rotation_cover_threshold_kg_dm_per_ha = 2200.0;

  /// The energy in the feed the farmer buys, MJ ME/kg DM. Baleage and hay sit
  /// below pasture; this is a decent baleage. PLACEHOLDER.
  double supplement_me_mj_per_kg_dm = 10.0;

  /// Whether the farmer may buy feed at all. A farm run without it will let
  /// stock lose condition, which is a legitimate thing to model and to compare
  /// against.
  bool may_buy_feed = true;

  /// Which system to run, and how much to buy at the floor.
  GrazingPreference preference = GrazingPreference::ByCover;
  FloorPurchase floor_purchase = FloorPurchase::WholeDemand;

  [[nodiscard]] std::string validation_error() const;
};

/// One purchase, on one day.
///
/// Recorded rather than accumulated, because "when did this farm need feed"
/// answers a different question from "how much did it need", and a report has
/// to answer both.
struct FeedPurchase {
  Date date;
  std::size_t mob = 0;
  std::string mob_name;
  double kg_dm = 0.0;

  /// Why the farmer bought it, in the terms the decision was made in.
  enum class Reason : std::uint8_t {
    /// The paddock could not meet the mob's demand.
    PaddockShort,
    /// The farm's cover was at the floor, so grazing harder was not an option.
    ProtectingCover,
  };
  Reason reason = Reason::PaddockShort;
};

[[nodiscard]] std::string to_string(FeedPurchase::Reason reason);
[[nodiscard]] std::string to_string(GrazingPreference preference);
[[nodiscard]] std::string to_string(FloorPurchase purchase);

/// Moves mobs according to a grazing calendar.
///
/// Deliberately simple, and the simplicity is the point: the farmer moves a mob
/// when it has been somewhere long enough, and sends it to whichever free
/// paddock has rested longest. A cleverer rule - picking on cover, or looking
/// ahead - would be a management model, and this project has no source for one
/// yet. What this rule can do is show what happens when the rules cannot be
/// kept, which is the thing worth measuring.
class Farmer {
 public:
  explicit Farmer(GrazingCalendar calendar);

  /// What one day of decisions came to.
  struct Day {
    GrazingSystem system = GrazingSystem::SetStocking;
    std::vector<MobMove> moves;

    /// Moves that had to break the spell rule because nothing was rested
    /// enough.
    int short_spells = 0;

    /// Mobs that were due to move and could not, because every other paddock
    /// was occupied. The other half of the shuffle: the grazing lengthens
    /// instead.
    int grazings_extended = 0;

    /// What the farmer decided to buy today, and why.
    std::vector<FeedPurchase> purchases;

    /// The system the farmer settled on, when they are choosing rather than
    /// following a calendar.
    GrazingSystem chosen_system = GrazingSystem::SetStocking;
  };

  /// Decides and applies today's moves. Call before Farm::step, so a mob that
  /// moves eats on the paddock it moved to.
  ///
  /// `went_short` names the mobs that could not get what they needed yesterday,
  /// which is what makes a farmer move early. Smith and Dawson's rule is "do
  /// not graze a pasture for more than three days" - a maximum, not a fixed
  /// period - and a mob standing on ground it has already eaten while other
  /// paddocks carry feed is not a farm anybody runs. Pass an empty set on the
  /// first day, or when the caller is not tracking it.
  Day decide(Farm& farm, const Date& date, const std::vector<bool>& went_short);

  /// As above, for a caller that does not track shortfalls. Moves only on the
  /// graze-length rule.
  Day decide(Farm& farm, const Date& date);

  [[nodiscard]] const GrazingCalendar& calendar() const noexcept { return calendar_; }

  /// Decides today without a calendar: which system to run, and what to buy.
  ///
  /// The two things this farmer will not do are let the sward be grazed out and
  /// let the stock go hungry, and when the pasture cannot deliver both it buys
  /// feed rather than choosing between them. Everything else - which system,
  /// which paddock, how much to feed out - follows from the state of the farm
  /// on the day.
  ///
  /// `diet` describes the pasture; the supplement's energy comes from the
  /// policy. Returns the decisions, and fills `supplement_kg_dm` with what to
  /// hand to each mob, ready to pass to Farm::step.
  Day manage(Farm& farm, const Date& date, const DietQuality& diet,
             const std::vector<bool>& went_short, std::vector<double>& supplement_kg_dm);

  [[nodiscard]] const ManagementPolicy& policy() const noexcept { return policy_; }

  void set_policy(ManagementPolicy policy);

 private:
  /// Which system the farm can afford today.
  [[nodiscard]] GrazingSystem system_for(const Farm& farm, const Date& date) const;

  /// The farm's mean cover, which both the system choice and the feed decision
  /// are made against. One definition so the two cannot disagree.
  [[nodiscard]] static double mean_cover(const Farm& farm);

  GrazingCalendar calendar_;
  ManagementPolicy policy_;
};

}  // namespace paddock::core
