/**
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 *
 * @format
 */

export default {
  setupFiles: ['./jest.setup.js'],
  testRegex: '/tests/.*\\.test\\.ts$',
  extensionsToTreatAsEsm: ['.ts'],
  // Tests import the package by its upstream name 'yoga-layout' so test sources
  // stay byte-identical to upstream (avoids merge conflicts on every gentest
  // regeneration). Resolve it to local sources here; the published package name
  // remains 'better-yoga-layout'.
  moduleNameMapper: {
    '^yoga-layout$': '<rootDir>/src/index.ts',
  },
};
