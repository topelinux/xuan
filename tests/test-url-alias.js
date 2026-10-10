import assert from 'tjs:assert';
import path from 'tjs:path';
import url from 'url';
import nodeUrl from 'node:url';

assert.eq(typeof url.URL, 'function');
assert.eq(typeof url.URLSearchParams, 'function');
assert.eq(typeof url.fileURLToPath, 'function');
assert.eq(typeof url.pathToFileURL, 'function');
assert.eq(typeof nodeUrl.URL, 'function');

const cwd = tjs.cwd;
const file = path.join(cwd, 'tests', 'test-url-alias.js');
const fileUrl = url.pathToFileURL(file);

assert.eq(fileUrl.protocol, 'file:');
assert.eq(url.fileURLToPath(fileUrl), file);
assert.eq(nodeUrl.fileURLToPath(fileUrl), file);

const requiredUrlNs = require('url');
const requiredUrl = requiredUrlNs.default ?? requiredUrlNs;
assert.eq(typeof requiredUrl.fileURLToPath, 'function');
assert.eq(requiredUrl.fileURLToPath(fileUrl), file);
