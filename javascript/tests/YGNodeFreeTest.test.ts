/**
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

import Yoga from 'yoga-layout';

test('node exposes free and freeRecursive', () => {
  const node = Yoga.Node.create();
  expect(typeof node.free).toBe('function');
  expect(typeof node.freeRecursive).toBe('function');
  node.free();
});

test('freeRecursive frees a laid-out tree and detaches children', () => {
  const root = Yoga.Node.create();
  const child = Yoga.Node.create();
  const grandchild = Yoga.Node.create();
  root.insertChild(child, 0);
  child.insertChild(grandchild, 0);
  root.calculateLayout(100, 100);
  expect(root.getComputedWidth()).toBe(100);

  root.freeRecursive();
  expect(root.getChildCount()).toBe(0);
  expect(child.getChildCount()).toBe(0);
});

test('free is idempotent (double free is a no-op)', () => {
  const root = Yoga.Node.create();
  const child = Yoga.Node.create();
  root.insertChild(child, 0);
  root.freeRecursive();
  // Calling again must not throw or double-free native memory.
  expect(() => root.freeRecursive()).not.toThrow();
  expect(() => child.free()).not.toThrow();
});

test('freeing a child detaches it from its parent', () => {
  const root = Yoga.Node.create();
  const child = Yoga.Node.create();
  root.insertChild(child, 0);
  expect(root.getChildCount()).toBe(1);

  child.free();
  expect(root.getChildCount()).toBe(0);
  root.free();
});

test('Config exposes an idempotent free', () => {
  const config = Yoga.Config.create();
  expect(typeof config.free).toBe('function');
  const node = Yoga.Node.createWithConfig(config);
  node.free();
  config.free();
  expect(() => config.free()).not.toThrow();
});

test('Yoga.Node.destroy delegates to free and detaches the node', () => {
  expect(typeof Yoga.Node.destroy).toBe('function');
  const root = Yoga.Node.create();
  const child = Yoga.Node.create();
  root.insertChild(child, 0);

  Yoga.Node.destroy(child);
  expect(root.getChildCount()).toBe(0);
  Yoga.Node.destroy(root);
});
