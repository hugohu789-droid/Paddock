// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#pragma once

#include <QApplication>

namespace paddock::app {

/// The window's colours, in one place.
///
/// **A dark instrument panel, because that is what this is.** The scene is a
/// lit farm under a night-dark sky and the charts are read against it; a
/// light-grey system dialogue around them made the map look like a photograph
/// pasted into a form. Dark also stops the panel competing with the one thing
/// on screen that is actually coloured by data.
///
/// Applied as a palette plus a style sheet rather than as a theme file, so it
/// travels with the binary and looks the same on a machine whose desktop is set
/// to something else. Nothing here changes what any control does.
void apply_theme(QApplication& application);

}  // namespace paddock::app
