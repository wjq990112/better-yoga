/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <yoga/algorithm/Cache.h>
#include <yoga/algorithm/PixelGrid.h>
#include <yoga/numeric/Comparison.h>

namespace facebook::yoga {

static inline bool sizeIsExactAndMatchesOldMeasuredSize(
    SizingMode sizeMode,
    float size,
    float lastComputedSize) {
  return sizeMode == SizingMode::StretchFit &&
      yoga::inexactEquals(size, lastComputedSize);
}

static inline bool oldSizeIsMaxContentAndStillFits(
    SizingMode sizeMode,
    float size,
    SizingMode lastSizeMode,
    float lastComputedSize) {
  return sizeMode == SizingMode::FitContent &&
      lastSizeMode == SizingMode::MaxContent &&
      (size >= lastComputedSize || yoga::inexactEquals(size, lastComputedSize));
}

static inline bool newSizeIsStricterAndStillValid(
    SizingMode sizeMode,
    float size,
    SizingMode lastSizeMode,
    float lastSize,
    float lastComputedSize) {
  return lastSizeMode == SizingMode::FitContent &&
      sizeMode == SizingMode::FitContent && yoga::isDefined(lastSize) &&
      yoga::isDefined(size) && yoga::isDefined(lastComputedSize) &&
      lastSize > size &&
      (lastComputedSize <= size || yoga::inexactEquals(size, lastComputedSize));
}

CachedMeasurementRequest makeCachedMeasurementRequest(
    const SizingMode widthMode,
    const float availableWidth,
    const SizingMode heightMode,
    const float availableHeight,
    const float marginRow,
    const float marginColumn,
    const yoga::Config* const config) {
  const float pointScaleFactor = config->getPointScaleFactor();
  const bool useRoundedComparison = config != nullptr && pointScaleFactor != 0;

  return CachedMeasurementRequest{
      .widthMode = widthMode,
      .heightMode = heightMode,
      .availableWidth = availableWidth,
      .availableHeight = availableHeight,
      .marginRow = marginRow,
      .marginColumn = marginColumn,
      .effectiveWidth = useRoundedComparison
          ? roundValueToPixelGrid(availableWidth, pointScaleFactor, false, false)
          : availableWidth,
      .effectiveHeight = useRoundedComparison
          ? roundValueToPixelGrid(
                availableHeight, pointScaleFactor, false, false)
          : availableHeight,
      .pointScaleFactor = pointScaleFactor,
      .useRoundedComparison = useRoundedComparison,
  };
}

bool canUseCachedMeasurementForEntry(
    const CachedMeasurementRequest& request,
    const SizingMode lastWidthMode,
    const float lastAvailableWidth,
    const SizingMode lastHeightMode,
    const float lastAvailableHeight,
    const float lastComputedWidth,
    const float lastComputedHeight) {
  if ((yoga::isDefined(lastComputedHeight) && lastComputedHeight < 0) ||
      ((yoga::isDefined(lastComputedWidth)) && lastComputedWidth < 0)) {
    return false;
  }

  const float effectiveLastWidth = request.useRoundedComparison
      ? roundValueToPixelGrid(
            lastAvailableWidth, request.pointScaleFactor, false, false)
      : lastAvailableWidth;
  const float effectiveLastHeight = request.useRoundedComparison
      ? roundValueToPixelGrid(
            lastAvailableHeight, request.pointScaleFactor, false, false)
      : lastAvailableHeight;

  const bool hasSameWidthSpec = lastWidthMode == request.widthMode &&
      yoga::inexactEquals(effectiveLastWidth, request.effectiveWidth);
  const bool hasSameHeightSpec = lastHeightMode == request.heightMode &&
      yoga::inexactEquals(effectiveLastHeight, request.effectiveHeight);

  const bool widthIsCompatible =
      hasSameWidthSpec ||
      sizeIsExactAndMatchesOldMeasuredSize(
          request.widthMode,
          request.availableWidth - request.marginRow,
          lastComputedWidth) ||
      oldSizeIsMaxContentAndStillFits(
          request.widthMode,
          request.availableWidth - request.marginRow,
          lastWidthMode,
          lastComputedWidth) ||
      newSizeIsStricterAndStillValid(
          request.widthMode,
          request.availableWidth - request.marginRow,
          lastWidthMode,
          lastAvailableWidth,
          lastComputedWidth);

  const bool heightIsCompatible = hasSameHeightSpec ||
      sizeIsExactAndMatchesOldMeasuredSize(
                                      request.heightMode,
                                      request.availableHeight -
                                          request.marginColumn,
                                      lastComputedHeight) ||
      oldSizeIsMaxContentAndStillFits(request.heightMode,
                                      request.availableHeight -
                                          request.marginColumn,
                                      lastHeightMode,
                                      lastComputedHeight) ||
      newSizeIsStricterAndStillValid(request.heightMode,
                                     request.availableHeight -
                                         request.marginColumn,
                                     lastHeightMode,
                                     lastAvailableHeight,
                                     lastComputedHeight);

  return widthIsCompatible && heightIsCompatible;
}

bool canUseCachedMeasurement(
    const SizingMode widthMode,
    const float availableWidth,
    const SizingMode heightMode,
    const float availableHeight,
    const SizingMode lastWidthMode,
    const float lastAvailableWidth,
    const SizingMode lastHeightMode,
    const float lastAvailableHeight,
    const float lastComputedWidth,
    const float lastComputedHeight,
    const float marginRow,
    const float marginColumn,
    const yoga::Config* const config) {
  const CachedMeasurementRequest request = makeCachedMeasurementRequest(
      widthMode,
      availableWidth,
      heightMode,
      availableHeight,
      marginRow,
      marginColumn,
      config);
  return canUseCachedMeasurementForEntry(
      request,
      lastWidthMode,
      lastAvailableWidth,
      lastHeightMode,
      lastAvailableHeight,
      lastComputedWidth,
      lastComputedHeight);
}

} // namespace facebook::yoga
