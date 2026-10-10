import assert from 'tjs:assert';
import path from 'tjs:path';


async function testCliVersion() {
    const args = [
        tjs.exePath,
        '-v'
    ];
    const proc = tjs.spawn(args, { stdout: 'pipe', stderr: 'ignore' });
    const stdoutStr = await proc.stdout.text();
    assert.eq(stdoutStr.trim(), `v${tjs.version}`, 'returns the right version');
}

async function testCliHelp() {
    const args = [
        tjs.exePath,
        '-h'
    ];
    const proc = tjs.spawn(args, { stdout: 'pipe', stderr: 'ignore' });
    const stdoutStr = await proc.stdout.text();
    assert.ok(stdoutStr.startsWith('Usage: '), 'returns the help');
}

async function testCliBadOption() {
    const args = [
        tjs.exePath,
        '--foo'
    ];
    const proc = tjs.spawn(args, { stdout: 'ignore', stderr: 'pipe' });
    const stderrStr = await proc.stderr.text();
    assert.ok(stderrStr.includes('unrecognized option: foo'), 'recognizes a bad option');
}

async function testCliImplicitRun() {
    const scriptPath = path.join(import.meta.dirname, 'helpers', 'cjs-entry.cjs');
    const scriptArgs = [ 'foo', 'bar' ];

    const runProc = tjs.spawn([ tjs.exePath, 'run', scriptPath, ...scriptArgs ], { stdout: 'pipe', stderr: 'pipe' });
    const implicitProc = tjs.spawn([ tjs.exePath, scriptPath, ...scriptArgs ], { stdout: 'pipe', stderr: 'pipe' });

    const runStdout = await runProc.stdout.text();
    const runStatus = await runProc.wait();
    const implicitStdout = await implicitProc.stdout.text();
    const implicitStatus = await implicitProc.wait();

    assert.eq(runStatus.exit_status, 0, 'explicit run exits successfully');
    assert.eq(implicitStatus.exit_status, 0, 'implicit run exits successfully');
    assert.eq(implicitStdout.trim(), runStdout.trim(), 'implicit run matches explicit run output');
}

async function testCliEvalShortOption() {
    const proc = tjs.spawn([ tjs.exePath, '-e', 'console.log(40 + 2)' ], { stdout: 'pipe', stderr: 'pipe' });
    const stdoutStr = await proc.stdout.text();
    const status = await proc.wait();

    assert.eq(status.exit_status, 0, '-e exits successfully');
    assert.eq(stdoutStr.trim(), '42', '-e evaluates expression');
}

await testCliVersion();
await testCliHelp();
await testCliBadOption();
await testCliImplicitRun();
await testCliEvalShortOption();
