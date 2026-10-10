import assert from 'tjs:assert';
import npm from 'tjs:npm';
import { run } from 'tjs:npm';

assert.equal(typeof run, 'function');
assert.equal(typeof npm.run, 'function');
