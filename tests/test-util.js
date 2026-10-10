import assert from 'tjs:assert';
import util, { format, formatWithOptions, getSystemErrorName, inspect, inherits, types } from 'util';
import nodeUtil from 'node:util';

assert.eq(typeof format, 'function');
assert.eq(typeof inspect, 'function');
assert.eq(typeof formatWithOptions, 'function');
assert.eq(format('%s:%d', 'port', 8080), 'port:8080');
assert.eq(formatWithOptions({}, '%s %s', 'hello', 'world'), 'hello world');
assert.ok(inspect({ a: 1 }).includes('a'));

assert.eq(typeof util.types.isTypedArray, 'function');
assert.eq(typeof types.isMap, 'function');
assert.eq(types.isMap(new Map()), true);
assert.eq(types.isSet(new Set()), true);
assert.eq(types.isWeakMap(new WeakMap()), true);
assert.eq(types.isWeakSet(new WeakSet()), true);
assert.eq(types.isTypedArray(new Uint8Array(4)), true);
assert.eq(types.isDataView(new DataView(new ArrayBuffer(8))), true);
assert.eq(types.isArrayBuffer(new ArrayBuffer(8)), true);
assert.eq(types.isAnyArrayBuffer(new SharedArrayBuffer(8)), true);
assert.eq(types.isPromise(Promise.resolve()), true);
assert.eq(getSystemErrorName(-2), 'ENOENT');

function Base() {}
function Child() {}
inherits(Child, Base);
assert.eq(Child.super_, Base);
assert.ok(Child.prototype instanceof Base);

const requiredUtilNs = require('util');
const requiredNodeUtilNs = require('node:util');
const requiredUtil = requiredUtilNs.default ?? requiredUtilNs;
const requiredNodeUtil = requiredNodeUtilNs.default ?? requiredNodeUtilNs;
assert.eq(typeof requiredUtil.format, 'function');
assert.eq(typeof requiredUtil.types.isMap, 'function');
assert.eq(requiredUtil.format('%s', 'ok'), 'ok');
assert.eq(typeof requiredNodeUtil.format, 'function');
assert.eq(requiredNodeUtil.types.isMap(new Map()), true);
assert.eq(typeof nodeUtil.format, 'function');
