import assert from 'tjs:assert';
import path from 'tjs:path';

const args = [
    tjs.exePath,
    'run',
    path.join(import.meta.dirname, 'helpers', 'unhandled-rejection-listener.js')
];
const proc = tjs.spawn(args, { stdout: 'pipe', stderr: 'pipe' });
const [ status, stdoutStr, stderrStr ] = await Promise.all([ proc.wait(), proc.stdout.text(), proc.stderr.text() ]);

assert.eq(status.exit_status, 0, 'listener keeps process alive');
assert.eq(status.term_signal, null, 'process exits normally');
assert.ok(stdoutStr.includes('handled:oops!'), 'listener receives rejection reason');
assert.eq(stderrStr.trim(), '', 'default stderr output is suppressed when listener exists');
