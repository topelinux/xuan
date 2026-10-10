import assert from 'tjs:assert';

const proc = tjs.spawn([ tjs.exePath, '-v' ], { stdout: 'pipe', stderr: 'pipe' });
const stdout = await proc.stdout.text();

// Let the close callback run before calling wait().
await new Promise(resolve => setTimeout(resolve, 0));

const status = await proc.wait();

assert.ok(stdout.trim().startsWith('v'));
assert.eq(status.exit_status, 0);
assert.eq(status.term_signal, null);
