import assert from 'tjs:assert';
import path from 'tjs:path';

const args = [
    tjs.exePath,
    'run',
    path.join(import.meta.dirname, 'helpers', 'unhandled-rejection-handled-chain.js')
];

const proc = tjs.spawn(args, { stdout: 'pipe', stderr: 'pipe' });
const [ status, stdoutStr, stderrStr ] = await Promise.all([ proc.wait(), proc.stdout.text(), proc.stderr.text() ]);

assert.eq(status.exit_status, 0, 'handled rejections should not trigger unhandledRejection');
assert.eq(status.term_signal, null, 'process exits normally');
assert.eq(stdoutStr.trim(), '', 'no unexpected stdout output');
assert.eq(stderrStr.trim(), '', 'no unexpected stderr output');
