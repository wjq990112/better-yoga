/**
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

import Yoga from 'better-yoga-layout';

import {getMeasureCounterMax} from './tools/MeasureCounter.ts';

test('measure_once_single_flexible_child', () => {
  const root = Yoga.Node.create();
  root.setFlexDirection(Yoga.FLEX_DIRECTION_ROW);
  root.setAlignItems(Yoga.ALIGN_FLEX_START);
  root.setWidth(100);
  root.setHeight(100);

  const measureCounter = getMeasureCounterMax();

  const root_child0 = Yoga.Node.create();
  root_child0.setMeasureFunc(measureCounter.inc);
  root_child0.setFlexGrow(1);
  root.insertChild(root_child0, 0);

  root.calculateLayout(undefined, undefined, Yoga.DIRECTION_LTR);

  expect(measureCounter.get()).toBe(1);
});

test('measure_reruns_after_mark_dirty_at_unchanged_size', () => {
  const root = Yoga.Node.create();
  root.setWidth(100);
  root.setHeight(100);

  const measureCounter = getMeasureCounterMax();

  const root_child0 = Yoga.Node.create();
  root_child0.setMeasureFunc(measureCounter.inc);
  root.insertChild(root_child0, 0);

  root.calculateLayout(undefined, undefined, Yoga.DIRECTION_LTR);
  expect(measureCounter.get()).toBe(1);

  // Re-layout with nothing changed: the cached measurement is reused and the
  // measure function is not re-invoked.
  root.calculateLayout(undefined, undefined, Yoga.DIRECTION_LTR);
  expect(measureCounter.get()).toBe(1);

  // markDirty() signals the node's content changed, so the next layout must
  // re-run the measure function even though the available size is identical.
  // Regression: the JS-side measure cache keyed only on the measure spec
  // (width/height + modes) with no dirty-awareness, so a markDirty()'d node
  // kept returning its stale cached result and the measure func never ran.
  root_child0.markDirty();
  root.calculateLayout(undefined, undefined, Yoga.DIRECTION_LTR);
  expect(measureCounter.get()).toBe(2);
});
