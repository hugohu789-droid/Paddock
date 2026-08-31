// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

/// Where a number came from, carried with the number.
///
/// `core` computes and does not care; this layer vouches. That split is
/// deliberate: an equation is the same equation whether its inputs are
/// published or guessed, and burying the difference in a comment beside the
/// value is how a guess ends up quoted as a finding.
namespace paddock::config {

/// The four things a number can be, in descending order of how much weight it
/// will bear.
enum class Provenance : std::uint8_t {
  /// Explicitly stated in the cited source. DairyNZ's 59 MJ ME/day at 500 kg.
  Direct,

  /// Arithmetic on values the source states, and nothing more. Beef + Lamb NZ
  /// give 300 kg and 60% of mature weight, so 500 kg is Derived - it is not an
  /// Angus mature weight and must never be recorded as one.
  Derived,

  /// The source looks as though it contains this, but the reading or the
  /// transcription needs confirming before the value carries any weight.
  Verify,

  /// Calibrated so that published observations reproduce, rather than stated by
  /// any source. Not evidence-free like a Placeholder, and not a citation like
  /// a Direct value: a fitted number is only as good as the observations it was
  /// fitted to, and a report must say which those were.
  ///
  /// It exists because facial eczema needed it. The step from toxin load to
  /// serum GGT cannot be derived - working the sourced numbers through gives an
  /// answer sixteen times out of step with field observation - so it is fitted
  /// to the published spore-count thresholds and labelled as fitted. Calling
  /// that Placeholder would understate it and Derived would overstate it.
  Fitted,

  /// An engineering value with no evidence behind it. The model will run on it;
  /// nothing may be published from it.
  Placeholder,
};

[[nodiscard]] std::string to_string(Provenance status);

/// Parses one of the four words. Returns false for anything else rather than
/// guessing, because a status nobody chose is worse than no status.
[[nodiscard]] bool provenance_from_string(std::string_view text, Provenance& out);

/// A number and the account of where it came from.
///
/// The value is always present, even when the status is Verify or Placeholder:
/// the model has to run on something. What changes with the status is whether
/// any result that depends on it may be quoted.
struct SourcedValue {
  double value = 0.0;
  Provenance status = Provenance::Placeholder;

  /// An identifier into data/calibration/livestock/sources.toml. Required for
  /// Direct and Derived - a citation with nothing to cite is decoration - and
  /// empty is allowed for Verify and Placeholder, which are by definition not
  /// yet attached to a source. Fitted values must name what they were fitted to.
  std::string source_id;

  /// True when this number rests on something somebody published.
  ///
  /// Fitted counts, and that is the point of including it: a value calibrated
  /// against observations has to name which observations, or nobody can tell
  /// what it would take to falsify it.
  [[nodiscard]] bool is_evidence() const noexcept {
    return status == Provenance::Direct || status == Provenance::Derived ||
           status == Provenance::Fitted;
  }

  /// Empty when the value and its status are consistent; otherwise what is
  /// wrong. `context` names the parameter for the message.
  [[nodiscard]] std::string validation_error(const std::string& context) const;
};

/// The source identifiers a manifest declares, used to catch a citation that
/// points at nothing.
[[nodiscard]] std::vector<std::string> load_source_ids(const std::string& manifest_path);

}  // namespace paddock::config
