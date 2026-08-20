// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#pragma once

#include <QString>

namespace paddock::app {

/// Writes a report to a PDF sized for A4.
///
/// **One place, because two reports printing differently is two bugs.** The
/// comparison and the run report had the same six lines of set-up copied
/// between them, which is how they came to share the same fault.
///
/// The fault is worth naming. A QPdfWriter defaults to 1,200 dots per inch, so
/// its width() and height() are in twelve-hundredths of an inch - and handing
/// those to QTextDocument::setPageSize() lays the text out on a page ten
/// thousand units wide while the fonts stay at their ordinary point sizes. The
/// result prints legibly only under a magnifying glass. QTextDocument::print()
/// already knows how to fit a paged device; it needs no page size from us.
///
/// `markdown` is the report as it was generated. `title` goes in the PDF's own
/// metadata, where a print queue and a file manager both look for it.
/// Returns false when the file could not be written.
[[nodiscard]] bool print_markdown_to_pdf(const QString& path, const QString& markdown,
                                         const QString& title);

/// The same, for a report that is already HTML.
[[nodiscard]] bool print_html_to_pdf(const QString& path, const QString& html,
                                     const QString& title);

}  // namespace paddock::app
