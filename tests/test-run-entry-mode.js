import assert from 'tjs:assert';
import path from 'tjs:path';


async function runAndParse(scriptName) {
    const scriptPath = path.join(import.meta.dirname, 'helpers', scriptName);
    const args = [ tjs.exePath, 'run', scriptPath ];
    const proc = tjs.spawn(args, { stdout: 'pipe', stderr: 'pipe' });
    const stdout = await proc.stdout.text();
    const status = await proc.wait();

    assert.eq(status.exit_status, 0, `${scriptName} runs`);
    return JSON.parse(stdout.trim());
}

async function testRunCjsEntry() {
    const data = await runAndParse('cjs-entry.cjs');

    assert.eq(data.mode, 'cjs', 'uses CJS entry mode');
    assert.eq(data.exportsType, 'object', 'provides exports in CJS entry');
    assert.eq(data.moduleType, 'object', 'provides module in CJS entry');
    assert.eq(data.answer, 42, 'keeps exports assignment');
}

async function testRunEsmEntry() {
    const data = await runAndParse('esm-entry.mjs');

    assert.eq(data.mode, 'esm', 'uses ESM entry mode');
    assert.eq(data.exportsType, 'undefined', 'does not expose exports in ESM entry');
    assert.eq(data.moduleType, 'undefined', 'does not expose module in ESM entry');
}

async function testRunJsEntryInNoTypePackage() {
    const data = await runAndParse('no-type-package/entry.js');

    assert.eq(data.exportsType, 'object', 'uses CommonJS for .js inside package without type');
    assert.eq(data.moduleType, 'object', 'provides module in package without type');
    assert.eq(data.requireType, 'function', 'provides require in package without type');
    assert.eq(data.value, 42, 'resolves relative require from package without type');
}

await testRunCjsEntry();
await testRunEsmEntry();
await testRunJsEntryInNoTypePackage();
