// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#include <algorithm>
#include <string>
#include <utility>

#include <paddock/core/FeedStore.hpp>

namespace paddock::core {

namespace {

/// A loss is a share, and a share of one would mean nothing survived.
std::string share_error(double value, const char* name) {
  if (value < 0.0 || value >= 1.0) {
    return std::string(name) + " is a share and must lie in [0, 1)";
  }
  return {};
}

}  // namespace

std::string ConservationLosses::invalid_reason() const {
  for (const auto& [value, name] :
       {std::pair<double, const char*>{cutting_loss_fraction, "cutting_loss_fraction"},
        std::pair<double, const char*>{storage_loss_fraction, "storage_loss_fraction"},
        std::pair<double, const char*>{feed_out_loss_fraction, "feed_out_loss_fraction"}}) {
    if (const std::string trouble = share_error(value, name); !trouble.empty()) {
      return trouble;
    }
  }
  if (silage_energy_mj_per_kg_dm <= 0.0) {
    return "silage has to be worth something to an animal";
  }
  if (silage_digestibility_percent <= 0.0 || silage_digestibility_percent >= 100.0) {
    return "silage digestibility is a percentage of the dry matter";
  }
  return {};
}

std::string ConservationPolicy::invalid_reason() const {
  if (surplus_cover_kg_dm_per_ha <= 0.0) {
    return "the cover a surplus starts at must be positive";
  }
  if (cut_to_cover_kg_dm_per_ha <= 0.0) {
    return "a cut has to leave something behind";
  }
  if (cut_to_cover_kg_dm_per_ha >= surplus_cover_kg_dm_per_ha) {
    return "a cut that leaves more than it started with is not a cut";
  }
  const auto valid = [](int month, int day) { return month >= 1 && month <= 12 && day >= 1; };
  if (!valid(first_cut_month, first_cut_day) || !valid(last_cut_month, last_cut_day)) {
    return "the cutting window is not made of dates";
  }
  return {};
}

bool ConservationPolicy::may_cut_on(int month, int day) const {
  if (!conserves) {
    return false;
  }
  const int today = (month * 100) + day;
  const int opens = (first_cut_month * 100) + first_cut_day;
  const int closes = (last_cut_month * 100) + last_cut_day;

  // A window inside one calendar year, or one that crosses new year - a spring
  // in the southern hemisphere does not, but a policy written for a northern
  // farm would, and refusing to represent it would be an accident of latitude.
  return opens <= closes ? (today >= opens && today <= closes)
                         : (today >= opens || today <= closes);
}

double FeedStore::add(double standing_kg_dm, const ConservationLosses& losses) {
  const double cut = std::max(0.0, standing_kg_dm);
  if (cut <= 0.0) {
    return 0.0;
  }

  // **What goes in is less than what was cut.** The mower leaves stubble, the
  // rake misses some, and it respires while it wilts.
  const double into_stack =
      cut * (1.0 - losses.cutting_loss_fraction) * (1.0 - losses.storage_loss_fraction);

  held_kg_dm_ += into_stack;
  cut_kg_dm_ += cut;
  lost_kg_dm_ += cut - into_stack;
  return into_stack;
}

double FeedStore::take(double wanted_kg_dm, const ConservationLosses& losses) {
  if (wanted_kg_dm <= 0.0 || held_kg_dm_ <= 0.0) {
    return 0.0;
  }

  // **Asked for what reaches the animal, not for what leaves the stack.** A
  // mob wanting 100 kg needs more than 100 taken out, because some of it will
  // be trodden and refused - and a store that handed over exactly what was
  // asked would be feeding stock on losses.
  const double keeps = 1.0 - losses.feed_out_loss_fraction;
  const double out_of_stack = keeps > 0.0 ? std::min(held_kg_dm_, wanted_kg_dm / keeps) : 0.0;

  held_kg_dm_ -= out_of_stack;

  const double eaten = out_of_stack * keeps;
  fed_kg_dm_ += eaten;
  lost_kg_dm_ += out_of_stack - eaten;
  return eaten;
}

}  // namespace paddock::core
