import assert from 'tjs:assert';
import querystring from 'querystring';
import nodeQuerystring from 'node:querystring';

assert.eq(typeof querystring.stringify, 'function');
assert.eq(typeof querystring.parse, 'function');
assert.eq(typeof nodeQuerystring.stringify, 'function');

assert.eq(querystring.stringify({ a: 1, b: 'x y' }), 'a=1&b=x%20y');
assert.eq(nodeQuerystring.stringify({ a: true, b: ['x', 'y'] }), 'a=true&b=x&b=y');

const parsed = querystring.parse('a=1&b=x%20y&b=z');
assert.eq(parsed.a, '1');
assert.eq(Array.isArray(parsed.b), true);
assert.eq(parsed.b.length, 2);
assert.eq(parsed.b[0], 'x y');
assert.eq(parsed.b[1], 'z');

const requiredQsNs = require('querystring');
const requiredNodeQsNs = require('node:querystring');
const requiredQs = requiredQsNs.default ?? requiredQsNs;
const requiredNodeQs = requiredNodeQsNs.default ?? requiredNodeQsNs;

assert.eq(typeof requiredQs.stringify, 'function');
assert.eq(typeof requiredNodeQs.parse, 'function');
assert.eq(requiredQs.stringify({ x: 'ok' }), 'x=ok');
assert.eq(requiredNodeQs.parse('k=v').k, 'v');
