/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <yoga/algorithm/SizingMode.h>
#include <yoga/config/Config.h>

namespace facebook::yoga {

bool canUseCachedMeasurement(
    SizingMode widthMode,
    float availableWidth,
    SizingMode heightMode,
    float availableHeight,
    SizingMode lastWidthMode,
    float lastAvailableWidth,
    SizingMode lastHeightMode,
    float lastAvailableHeight,
    float lastComputedWidth,
    float lastComputedHeight,
    float marginRow,
    float marginColumn,
    const yoga::Config* config);

// Pre-computed, loop-invariant view of the *current* measurement request, used
// to drive a cache scan without recomputing the request side per entry.
//
// `canUseCachedMeasurement` derives four pixel-grid-rounded values: the request
// side (`effectiveWidth`/`effectiveHeight`, from the current available size) and
// the entry side (`effectiveLastWidth`/`effectiveLastHeight`, from a cached
// entry). Only the entry side varies across the entries of a scan, so hoisting
// the request-side work — the `getPointScaleFactor()` lookup, the
// `useRoundedComparison` decision, and the two `roundValueToPixelGrid` calls on
// `availableWidth`/`availableHeight` — out of the per-entry loop makes a scan
// over N entries do that work once instead of N times. The arithmetic is
// identical because `roundValueToPixelGrid` is a pure function of constant
// inputs across the loop.
struct CachedMeasurementRequest {
  SizingMode widthMode;
  SizingMode heightMode;
  float availableWidth;
  float availableHeight;
  float marginRow;
  float marginColumn;
  float effectiveWidth;
  float effectiveHeight;
  float pointScaleFactor;
  bool useRoundedComparison;
};

CachedMeasurementRequest makeCachedMeasurementRequest(
    SizingMode widthMode,
    float availableWidth,
    SizingMode heightMode,
    float availableHeight,
    float marginRow,
    float marginColumn,
    const yoga::Config* config);

bool canUseCachedMeasurementForEntry(
    const CachedMeasurementRequest& request,
    SizingMode lastWidthMode,
    float lastAvailableWidth,
    SizingMode lastHeightMode,
    float lastAvailableHeight,
    float lastComputedWidth,
    float lastComputedHeight);

} // namespace facebook::yoga
