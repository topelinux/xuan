import assert from 'tjs:assert';
import path from 'tjs:path';

function parseJsonOutput(stdout, stderr, label) {
    try {
        return JSON.parse(stdout.trim());
    } catch {
        throw new Error(`${label} stdout is not valid JSON\nstdout: ${stdout}\nstderr: ${stderr}`);
    }
}

const compileArgs = [
    tjs.exePath,
    'compile',
    path.join(import.meta.dirname, 'helpers', 'hello.js')
];
const proc = tjs.spawn(compileArgs);
const status = await proc.wait();

assert.ok(status.exit_status === 0 && status.term_signal === null, 'succeeded');

const newExe = navigator.userAgentData.platform === 'Windows' ? 'hello.exe' : 'hello';

const st = await tjs.stat(newExe);

assert.ok(st.isFile, 'is a regular file');

const proc2 = tjs.spawn(path.join(tjs.cwd, newExe), { stdout: 'pipe' });
const [ status2, stdoutStr ] = await Promise.all([ proc2.wait(), proc2.stdout.text() ]);
assert.ok(stdoutStr.match(/hello!/) !== null, 'runs');
assert.ok(status2.exit_status === 0 && status.term_signal === null, 'succeeded');

await tjs.remove(newExe);

const asyncSource = path.join(import.meta.dirname, 'helpers', 'compile-async.js');
const asyncExe = navigator.userAgentData.platform === 'Windows' ? 'compile-async.exe' : 'compile-async';
const compileAsyncArgs = [ tjs.exePath, 'compile', asyncSource, asyncExe ];
const proc3 = tjs.spawn(compileAsyncArgs);
const status3 = await proc3.wait();

assert.ok(status3.exit_status === 0 && status3.term_signal === null, 'async compile succeeded');

const proc4 = tjs.spawn(path.join(tjs.cwd, asyncExe), { stdout: 'pipe' });
const [ status4, asyncStdout ] = await Promise.all([ proc4.wait(), proc4.stdout.text() ]);

assert.ok(asyncStdout.includes('start'), 'compiled async program runs top-level code');
assert.ok(asyncStdout.includes('timer fired'), 'compiled async program keeps event loop alive');
assert.ok(status4.exit_status === 0 && status4.term_signal === null, 'compiled async program exits cleanly');

await tjs.remove(asyncExe);

const cjsSource = path.join(import.meta.dirname, 'helpers', 'compile-cjs-entry.cjs');
const cjsExe = navigator.userAgentData.platform === 'Windows' ? 'compile-cjs.exe' : 'compile-cjs';
const compileCjsArgs = [ tjs.exePath, 'compile', cjsSource, cjsExe ];
const proc5 = tjs.spawn(compileCjsArgs);
const status5 = await proc5.wait();

assert.ok(status5.exit_status === 0 && status5.term_signal === null, 'cjs compile succeeded');

const proc6 = tjs.spawn(path.join(tjs.cwd, cjsExe), { stdout: 'pipe', stderr: 'pipe' });
const [ status6, cjsStdout, cjsStderr ] = await Promise.all([ proc6.wait(), proc6.stdout.text(), proc6.stderr.text() ]);
const cjsData = parseJsonOutput(cjsStdout, cjsStderr, 'compiled cjs program');

assert.ok(status6.exit_status === 0 && status6.term_signal === null, 'compiled cjs program exits cleanly');
assert.eq(cjsData.exportsType, 'object', 'compiled cjs program defines exports');
assert.eq(cjsData.moduleType, 'object', 'compiled cjs program defines module');
assert.eq(cjsData.filenameType, 'string', 'compiled cjs program defines __filename');
assert.eq(cjsData.dirnameType, 'string', 'compiled cjs program defines __dirname');
assert.eq(cjsData.basename, cjsExe, 'compiled cjs program maps __filename to executable name');
assert.ok(cjsData.dirnameMatches, 'compiled cjs program keeps dirname aligned');
assert.eq(cjsData.moduleFilename, path.join(tjs.cwd, cjsExe), 'compiled cjs program exposes module.filename');
assert.eq(cjsData.modulePath, tjs.cwd, 'compiled cjs program exposes module.path');
assert.eq(cjsData.moduleLoaded, false, 'compiled cjs program keeps module.loaded false during execution');
assert.eq(cjsData.moduleRequireType, 'function', 'compiled cjs program exposes module.require');
assert.eq(cjsData.answer, 42, 'compiled cjs program shares exports with module.exports');
assert.eq(cjsData.mode, 'cjs', 'compiled cjs program supports module.exports mutation');

await tjs.remove(cjsExe);
