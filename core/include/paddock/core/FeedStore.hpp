// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#pragma once

#include <string>

/// Feed cut in a good month and fed back in a bad one.
///
/// **The mechanism that turns a good year's growth into a bad year's feed**, and
/// the one this model was missing. Intake ran almost flat across a decade -
/// 2,437, 2,432 and 2,425 kg DM/ha in the driest, an ordinary and the wettest
/// year - while growth ran 5.3 to 9.4 tonnes, so utilisation read 46% down to
/// 26% for the same farm doing the same thing. A real Canterbury farm shuts up
/// paddocks in spring, cuts the surplus, and feeds it back in a dry summer or a
/// cold winter. This model could buy feed and could not make any, so a wet
/// spring simply grew grass that died.
///
/// **Silage is not the grass it came from**, and that is the point of tracking
/// it rather than leaving it in the paddock. Dry matter is lost cutting it,
/// fermenting it and feeding it out, and what survives is a poorer feed: New
/// Zealand pasture silage measures 9.3 to 9.6 MJ ME/kg DM against the 10.5 of
/// the pasture, at 65 to 68% digestibility. A model that conserved feed without
/// those losses would make a surplus free, and it is not.
namespace paddock::core {

/// What it costs to cut, keep and feed out.
struct ConservationLosses {
  /// Dry matter lost between the standing crop and the stack: mower losses,
  /// what is left in the stubble, and what respires while it wilts.
  ///
  /// **PLACEHOLDER.** Published New Zealand hay figures span 10 to 71% for the
  /// whole chain depending on how badly it goes; this is the mid part of a
  /// competent job and no single figure has been read for it.
  double cutting_loss_fraction = 0.10;

  /// Dry matter lost in the stack, to fermentation and to what spoils.
  /// **PLACEHOLDER**, same reason.
  double storage_loss_fraction = 0.10;

  /// Dry matter lost putting it in front of stock - trampled, refused, blown
  /// away. **PLACEHOLDER.**
  double feed_out_loss_fraction = 0.05;

  /// What a kilogram of the stack is worth to an animal. **DIRECT**: New
  /// Zealand pasture silage measures 9.3 to 9.6 MJ ME/kg DM (NZGA, "Pasture
  /// silage in New Zealand - targets and current practice"), against 10.5 for
  /// the pasture it was cut from. Conserving feed costs quality as well as
  /// quantity.
  double silage_energy_mj_per_kg_dm = 9.45;

  /// Digestibility of the same, percent. Direct, from the same source: 65.5 to
  /// 67.9%.
  double silage_digestibility_percent = 66.7;

  [[nodiscard]] std::string invalid_reason() const;

  /// What reaches an animal's mouth out of a kilogram cut in the paddock.
  [[nodiscard]] double kept_fraction() const noexcept {
    return (1.0 - cutting_loss_fraction) * (1.0 - storage_loss_fraction) *
           (1.0 - feed_out_loss_fraction);
  }
};

/// What the farm has in the stack, in kg DM.
///
/// **Not a bank account.** Feed put in loses dry matter on the way in and on
/// the way out, so `add` and `take` are not inverses and were never meant to
/// be: cutting 1,000 kg of standing grass puts 900 in the stack and gets 770 of
/// it into a sheep.
class FeedStore {
 public:
  FeedStore() = default;

  /// Cuts `standing_kg_dm` out of a paddock and puts what survives in the
  /// stack. Returns what went in.
  double add(double standing_kg_dm, const ConservationLosses& losses);

  /// Takes up to `wanted_kg_dm` out of the stack for stock to eat, and returns
  /// what actually reaches them - less than what leaves the stack, because
  /// feeding out loses some.
  double take(double wanted_kg_dm, const ConservationLosses& losses);

  [[nodiscard]] double held_kg_dm() const noexcept { return held_kg_dm_; }

  /// Everything ever cut and everything ever fed, for a report that has to say
  /// what the year did rather than only where it ended.
  [[nodiscard]] double cut_kg_dm() const noexcept { return cut_kg_dm_; }

  [[nodiscard]] double fed_kg_dm() const noexcept { return fed_kg_dm_; }

  /// Dry matter that left the standing crop and never reached an animal. **The
  /// number that says whether conserving was worth it**: a farm that cut a
  /// surplus it never needed has simply thrown it away more slowly than letting
  /// it die in the paddock.
  [[nodiscard]] double lost_kg_dm() const noexcept { return lost_kg_dm_; }

 private:
  double held_kg_dm_ = 0.0;
  double cut_kg_dm_ = 0.0;
  double fed_kg_dm_ = 0.0;
  double lost_kg_dm_ = 0.0;
};

/// When a farmer shuts a paddock up and when they open the stack.
struct ConservationPolicy {
  /// Whether this farm conserves feed at all. A farm that does not is the same
  /// farm with this off, which is what makes it a management choice rather than
  /// a code path.
  bool conserves = true;

  /// Cover above which there is a surplus worth cutting, kg DM/ha.
  ///
  /// **A farmer cuts what the stock cannot keep up with.** Well above the
  /// grazing floors - those say what must be left, this says what is more than
  /// the mob can eat before it dies.
  ///
  /// **PLACEHOLDER, and a stand-in for a decision this model cannot yet make.**
  /// A farmer does not cut a farm; they shut up the paddocks that have got
  /// ahead of the rotation, and they judge those on pre-grazing cover - the
  /// paddock about to be grazed, which stands well above the farm mean. This
  /// compares the mean, so a figure lifted from extension material written
  /// about pre-graze covers is a category error: 2,600 was tried and never
  /// fired once, because a sheep farm's mean cover does not go there.
  ///
  /// 2,300 is where this farm's mean sits in a wet spring and not in a dry one,
  /// which gives the behaviour a conservation policy exists to show - silage
  /// made in the year that has a surplus and none in the year that has not.
  /// **The number is doing a mechanism's job**, and the mechanism is a
  /// per-paddock decision: shut up what the rotation has left behind. That is
  /// the improvement, not a better constant.
  double surplus_cover_kg_dm_per_ha = 2300.0;

  /// What a cut takes the paddock down to, kg DM/ha. Below this and the sward
  /// is being mown rather than harvested.
  double cut_to_cover_kg_dm_per_ha = 1500.0;

  /// **The end of the conserving season**, which is later than the end of the
  /// silage season and that difference is not modelled.
  ///
  /// NZGA on pasture silage: "The key to making high quality pasture silage in
  /// the 1994/95 season was to harvest pasture by the first week in November."
  /// After that the crop has gone to stem. A New Zealand farm does not stop
  /// conserving then, though - it stops making *silage* and starts making hay,
  /// which is bulkier, later and poorer.
  ///
  /// **This farm has no early-spring surplus at all**, which is why the window
  /// runs to the end of January. Measured over the cutting window its mean
  /// cover peaks at about 1,850 kg DM/ha in October and early November, and it
  /// does not reach 2,300 until December - a Canterbury dryland flock at 7 SU
  /// a hectare eats the spring flush as it arrives. The surplus, when there is
  /// one, is a summer one.
  ///
  /// **What is not modelled is the quality that costs.** Everything cut is
  /// valued as the silage the NZGA measured, 9.45 MJ ME/kg DM, where a January
  /// hay cut off a stemmy sward is well below that. A late cut is therefore
  /// flattered. See docs/validation/verify.md, E38.
  int last_cut_month = 1;
  int last_cut_day = 31;

  /// Earliest a farm shuts a paddock up. Before this the spring flush has not
  /// arrived and anything cut is feed the ewes needed.
  int first_cut_month = 10;
  int first_cut_day = 1;

  [[nodiscard]] std::string invalid_reason() const;

  /// Whether a cut is allowed on this date.
  [[nodiscard]] bool may_cut_on(int month, int day) const;
};

}  // namespace paddock::core
