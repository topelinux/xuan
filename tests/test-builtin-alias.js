import assert from 'tjs:assert';
import path from 'path';
import nodePath from 'node:path';

assert.eq(typeof path.join, 'function');
assert.eq(path.sep, nodePath.sep);

const requiredPathNs = require('path');
const requiredNodePathNs = require('node:path');
const requiredPath = requiredPathNs.default ?? requiredPathNs;
const requiredNodePath = requiredNodePathNs.default ?? requiredNodePathNs;

assert.eq(typeof requiredPath.join, 'function');
assert.eq(requiredPath.sep, requiredNodePath.sep);
assert.eq(requiredPath.sep, path.sep);
