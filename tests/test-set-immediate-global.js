import assert from 'tjs:assert';
import path from 'tjs:path';

const args = [
    tjs.exePath,
    'run',
    path.join(import.meta.dirname, 'helpers', 'log-set-immediate.js')
];
const proc = tjs.spawn(args, { stdout: 'pipe', stderr: 'pipe' });
const [ status, stdoutStr, stderrStr ] = await Promise.all([ proc.wait(), proc.stdout.text(), proc.stderr.text() ]);

assert.eq(status.exit_status, 0, 'setImmediate helper exits successfully');
assert.eq(stdoutStr.trim(), 'function', 'setImmediate is installed as a global');
assert.eq(stderrStr.trim(), '', 'setImmediate helper does not print errors');
