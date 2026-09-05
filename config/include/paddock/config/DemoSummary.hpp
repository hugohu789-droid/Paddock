// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <paddock/config/FarmDashboard.hpp>
#include <paddock/config/ScenarioRun.hpp>

/// Six numbers a person can act on, and what each of them is worth.
///
/// **A dashboard is not a demonstration.** `FarmDashboard` reports
/// twenty-seven indicators because an auditor wants all of them; somebody being
/// shown this farm for five minutes wants the few that answer the question they
/// asked, and a page that answers everything answers nothing in the time
/// available. This picks the handful that bear on one management decision and
/// says what each is worth.
///
/// **It invents no second truth system.** Every figure here comes out of the
/// dashboard that already built it, carrying the `Provenance` it was already
/// given and the band it was already measured against. `Confidence` below is a
/// rendering of those two facts in a customer's words - not a new judgement, and
/// deliberately not a place where a number can be promoted.
namespace paddock::config {

/// What irrigation is allowed to do to annual production before the model has
/// stopped behaving like a Canterbury pasture.
///
/// **Measured, not chosen.** Winchmore ran three irrigated treatments beside a
/// dryland control for twenty-five farm years. The demonstration waters at
/// FAO-56's p = 0.6 - as soon as the sward would be held back - which is a wet
/// regime, so the 15% and 20% soil-moisture treatments are its company: fifty
/// treatment-years whose per-year response ratios run 1.14 to 2.68, median 1.90,
/// with a 10th to 90th percentile of 1.23 to 2.46. These are that band, rounded
/// **inward** to 1.25 and 2.40.
///
/// **Inward, which is the strict direction and is deliberate.** A gate a
/// fraction tighter than its sample can raise a false alarm and can never wave
/// a bad model through, and a false alarm on a validation gate is a
/// conversation rather than a defect. `IrrigationResponseTest` re-reads the
/// file and fails if these stop sitting inside the percentiles they came from,
/// so the rounding cannot quietly become a widening.
///
/// The source is `data/calibration/winchmore-annual-production.csv`; the
/// derivation is E88 and the gate is E90.
constexpr double kIrrigationResponseLow = 1.25;
constexpr double kIrrigationResponseHigh = 2.40;

/// How much weight one outcome will bear, in descending order.
///
/// **Each of these is a statement about evidence, and the order is the order
/// `Provenance` already uses.** Nothing here can be set by hand: `confidence_of`
/// derives it from the indicator's own status and whether it was measured
/// against a published band, so a number cannot be promoted by editing this
/// file.
enum class Confidence : std::uint8_t {
  /// Measured against a figure somebody else published, with an independent
  /// value of its own. The strongest thing this project says about any number.
  Benchmarked,

  /// Reproduces published observations, having been calibrated to them.
  /// `Provenance::Fitted`. Not evidence-free and not independent evidence
  /// either: it is only as good as what it was fitted to, and the note says
  /// what that was.
  Calibrated,

  /// No external benchmark, but a line in a budget the conservation suite
  /// closes to 1e-9 on every commit. The model is not shown to be right about
  /// the world here; it is shown to be self-consistent, which is a real and
  /// much smaller claim.
  Conserved,

  /// `Provenance::Verify` - the source looks as though it supports this and the
  /// reading has not been confirmed. Interesting, not quotable.
  Exploratory,

  /// `Provenance::Placeholder`. An engineering value with no evidence behind
  /// it. The model runs on it; nothing may be published from it.
  DoNotQuote,
};

/// The label a customer reads.
[[nodiscard]] std::string to_string(Confidence level);

/// One line saying what that label permits. This is the key at the foot of the
/// page, and it is the difference between a customer who understands the
/// summary and one who quotes it.
[[nodiscard]] std::string confidence_meaning(Confidence level);

/// Where an indicator's confidence comes from. **The whole of the mapping, in
/// one function**, so that the page and any other reader of it cannot disagree.
///
/// A band plus a cited or arithmetic status is Benchmarked; `Fitted` is
/// Calibrated whether or not it has a band, because a value fitted to the very
/// observations it is then compared against is not independently benchmarked;
/// `Verify` is Exploratory and `Placeholder` is DoNotQuote. Anything left -
/// stated or derived, with nothing published to check it against - is Conserved,
/// which every water and dry-matter figure in this summary is.
[[nodiscard]] Confidence confidence_of(const Indicator& indicator);

/// One outcome, both ways round.
struct OutcomeRow {
  std::string name;

  /// Carried with the numbers rather than appended where they are printed, so
  /// a caller that renders this to CSV or to a slide cannot lose it.
  std::string unit;

  double before = 0.0;
  double after = 0.0;

  /// After minus before, in the same unit. Signed: a fall is a fall.
  double difference = 0.0;

  /// The difference as a share of `before`, where that means anything.
  ///
  /// **Empty rather than infinite when `before` is zero.** A rain-fed farm
  /// applies no water, and "375 mm, up an infinite percentage" is not a
  /// sentence anybody should be shown. Empty is also the honest answer when
  /// `before` is so small that the percentage is arithmetic rather than
  /// information.
  std::optional<double> percent;

  Confidence confidence = Confidence::DoNotQuote;

  /// The indicator's own note, which is where a Calibrated figure says what it
  /// was fitted to.
  std::string note;
};

/// An outcome deliberately left off the page, and why.
///
/// **Saying what was left out is part of the summary.** A page that quietly
/// drops the feed bill is a page that chose its metrics to flatter; one that
/// says "bought feed is not on this page because the model's purchases are
/// unbounded - see E71" is telling the customer something true about the tool.
struct OmittedOutcome {
  std::string name;
  std::string reason;
};

/// The whole page.
struct DemoSummary {
  std::string before_name;
  std::string after_name;

  std::vector<OutcomeRow> outcomes;
  std::vector<OmittedOutcome> omitted;

  /// The confidence levels this page actually uses, strongest first. The key
  /// explains these and no others - a legend listing levels nothing on the page
  /// carries is furniture.
  [[nodiscard]] std::vector<Confidence> levels_used() const;
};

/// Builds the summary from two dashboards and the runs behind them.
///
/// The dashboards supply every figure that has an indicator; the runs supply
/// the water that went on, which is a management input rather than an outcome
/// and so has no dashboard tile of its own.
///
/// An outcome the dashboards do not both carry is left out and recorded in
/// `omitted`, never shown as a zero: a run that reported nothing and a run that
/// reported nought are different facts.
[[nodiscard]] DemoSummary demo_summary(const FarmDashboard& before, const FarmDashboard& after,
                                       const RunSummary& before_run, const RunSummary& after_run);

/// The page as text, for a terminal or a slide.
[[nodiscard]] std::string as_text(const DemoSummary& summary);

}  // namespace paddock::config
