import test from 'node:test';
import assert from 'node:assert/strict';
import { canStart, showSuccess, backendGap, modelsForDisplay } from './presentation.js';

test('C logical source without preview fields renders from the configured emitter without changing C data', () => {
  const source = {type: 'SOURCE', logical_emitter: true, center: [100, 200], axis: [1, 0], size: [54, 40]};
  const mirror = {type: 'MIRROR', center: [400, 200], axis: [1, 0], size: [140, 12]};
  const reference = {...source, center: [63.97, 200], tip: [99.97, 200], direction: [1, 0]};
  const original = structuredClone(source);
  const displayed = modelsForDisplay([mirror, source], [reference]);
  assert.equal(displayed[0], mirror);
  assert.deepEqual(displayed[1].center, reference.center);
  assert.deepEqual(displayed[1].tip, reference.tip);
  assert.deepEqual(displayed[1].direction, reference.direction);
  assert.deepEqual(source, original);
  assert.deepEqual(modelsForDisplay([], [reference]), []);
  assert.equal(modelsForDisplay([reference], [reference])[0], reference);
  assert.throws(() => modelsForDisplay([source]), /返回菜单/);
});

test('only matching board readiness enables start, before the first pose', () => {
  const ready = {screen: 'game', game_state: 1, connected: true, board_phase: 'READY', board_ready: true, state: 'STALE'};
  assert.equal(canStart(ready, true), true);
  for (const changes of [{board_phase: 'CONFIGURING'}, {board_ready: false}, {connected: false}, {preview: true}, {game_state: 2}]) {
    assert.equal(canStart({...ready, ...changes}, true), false);
  }
  assert.equal(canStart(ready, false), false);
});

test('backend gaps retain one complete running scene and original timestamps without success', () => {
  const scene = {screen: 'game', game_state: 2, level_id: 2, models: [{type: 'PORTAL_A'}],
    segments: [[1, 2, 3, 4]], sequence: '18014398509481985', captured_ms: '42',
    valid: true, state: 'LIVE', success: true};
  const gap = backendGap(scene);
  assert.equal(gap.models, scene.models);
  assert.equal(gap.segments, scene.segments);
  assert.equal(gap.captured_ms, '42');
  assert.equal(gap.sequence, scene.sequence);
  assert.equal(gap.display_held, true);
  assert.equal(showSuccess(gap, true), false);
  assert.equal(showSuccess(scene, true), true);
  assert.equal(showSuccess(scene, false), false);
  assert.deepEqual(backendGap({...scene, game_state: 1}).models, []);
  assert.deepEqual(backendGap({...scene, screen: 'menu'}).segments, []);
});
