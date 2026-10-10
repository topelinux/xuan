import assert from 'tjs:assert';
import { execSync } from 'node:child_process';
import { Buffer } from 'node:buffer';

const outBuf = execSync('printf hi');
assert.ok(Buffer.isBuffer(outBuf));
assert.equal(outBuf.toString(), 'hi');

const outStr = execSync('printf hi', { encoding: 'utf8' });
assert.equal(outStr, 'hi');

let caught = null;
try {
    execSync('echo fail-msg 1>&2; exit 7');
} catch (err) {
    caught = err;
}

assert.ok(caught instanceof Error);
assert.equal(caught.status, 7);
assert.equal(caught.signal, null);
assert.ok(Buffer.isBuffer(caught.stdout));
assert.ok(Buffer.isBuffer(caught.stderr));
assert.ok(caught.message.includes('Command failed: echo fail-msg 1>&2; exit 7'));
assert.ok(caught.stderr.toString().includes('fail-msg'));
