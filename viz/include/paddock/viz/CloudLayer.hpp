// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#pragma once

namespace paddock::viz {

/// What a cloud should look like on one day.
///
/// **This is presentation, and it only ever reads.** The weather model decides
/// what the day was - how much of the sky's radiation reached the ground, how
/// much rain fell - and this carries that decision to the renderer. Nothing
/// here is allowed to travel the other way: a cloud drawn thicker must never
/// become a farm that grew less grass. The same separation the rest of this
/// project keeps between state, biology, management and orchestration.
///
/// **Two fields a cloud usually has are missing, and their absence is the
/// point.**
///
/// There is no wind direction. The weather series carries a speed and no
/// bearing (docs/verify.md E12), so a cloud that drifted one way would be
/// drawing the single thing the data does not have - and a viewer would read
/// it as the day's wind, because that is what drifting cloud means.
///
/// There is no cloud type. Telling cumulus from stratus needs a cloud base
/// height and a vertical structure; this project has one clearness index.
/// Deriving a type from "dull and raining, so stratus" is a rule somebody made
/// up, and a made-up rule that produces a confident picture is worse than no
/// picture.
struct CloudLayer {
  /// How much of the sky the cloud fills, 0 to 1.
  ///
  /// **The one field that is measured.** It comes from the clearness index
  /// Rs/Ra - the share of the sky's radiation that reached the ground - read
  /// against FAO-56 Eq. 35's Angstrom endpoints, 0.75 for full sun and 0.25
  /// for none of it direct.
  double coverage = 0.0;

  /// How far above the ground the cloud floats, and how deep it is, in metres
  /// of the scene's own units.
  ///
  /// Drawing choices, both of them, and they are not pretending otherwise. The
  /// scene sets them from the farm's extent so the cloud reads as cloud over a
  /// paddock rather than as fog or as a distant weather front, and a real base
  /// height would need a measurement this project does not have.
  double base_height = 0.0;
  double thickness = 0.0;
};

}  // namespace paddock::viz
