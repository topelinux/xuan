import assert from 'tjs:assert';


globalThis.global = 'foo';
assert.eq(global, 'foo', 'global tracks assignment');

assert.eq(typeof globalThis.window, 'undefined', 'window is not defined by default');

globalThis.self = 'foo';
assert.is(globalThis, self, 'globalThis is self');
