// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#pragma once

#include <string>
#include <vector>

#include <paddock/config/DiseaseConfig.hpp>
#include <paddock/core/Mycotoxin.hpp>
#include <paddock/core/Weather.hpp>

namespace paddock::config {

/// One place, ready to be reported on or compared with another.
struct DiseaseSite {
  std::string name;
  core::WeatherSeries weather;
};

/// **The unit of these reports is the decision, not the measurement.**
///
/// A spore count is a laboratory figure that means nothing to most readers. The
/// number a farm acts on is how many days it would have had stock on zinc, and
/// that comes straight from DairyNZ's published protocol - start at the full
/// dose when counts reach 30,000, stop only after three weeks at or below
/// 10,000. Replaying that against recorded weather turns "peak 461,158
/// spores/g" into "108 days of dosing, starting 12 March".
///
/// Two comparisons, and they answer different questions.
///
/// **Down the years, one farm.** How often, how bad, how long - which only a
/// decade can say. One year cannot distinguish a place where this happens from
/// a place where it happened once.
///
/// **Across farms, the same years.** Whether it is a question for this farm at
/// all. Here the table is summary statistics only: two ten-row tables side by
/// side is not a comparison, it is two tables.
///
/// **What neither report claims.** The model does not give the animals zinc.
/// The programme column is what the protocol would have told a farmer to do;
/// the GGT beside it is what the season would have done to an untreated mob.
/// Read together they say what was demanded and what was at stake - not what
/// happened to a farm that followed the advice.

/// One farm, year by year.
[[nodiscard]] std::string render_disease_years(const DiseaseSite& site,
                                               const DiseaseDefinition& disease);

/// Two or more farms over the same years, as summary statistics.
[[nodiscard]] std::string render_disease_comparison(const std::vector<DiseaseSite>& sites,
                                                    const DiseaseDefinition& disease);

}  // namespace paddock::config
