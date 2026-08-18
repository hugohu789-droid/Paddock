// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#include <cstddef>
#include <vector>

#include <paddock/viz/ColourTable.hpp>

namespace paddock::viz {

namespace {

constexpr double kColourMaximum = 255.0;

}  // namespace

void fill_lookup_table(vtkLookupTable* table, const ColourScale& scale, int entries) {
  if (table == nullptr || entries <= 0) {
    return;
  }
  const std::vector<Rgb> ramp = scale.lookup_table(entries);
  table->SetNumberOfTableValues(static_cast<vtkIdType>(ramp.size()));
  table->SetTableRange(scale.minimum(), scale.maximum());
  for (std::size_t i = 0; i < ramp.size(); ++i) {
    table->SetTableValue(static_cast<vtkIdType>(i), ramp[i].red / kColourMaximum,
                         ramp[i].green / kColourMaximum, ramp[i].blue / kColourMaximum, 1.0);
  }
  table->Build();
}

}  // namespace paddock::viz
