import assert from 'tjs:assert';
import path from 'tjs:path';


async function testEntryScript(scriptName) {
    const scriptPath = path.join(import.meta.dirname, 'helpers', scriptName);
    const args = [ tjs.exePath, 'run', scriptPath ];
    const proc = tjs.spawn(args, { stdout: 'pipe', stderr: 'pipe' });
    const stdout = await proc.stdout.text();
    const status = await proc.wait();

    assert.eq(status.exit_status, 0, `${scriptName} runs`);

    const data = JSON.parse(stdout.trim());
    assert.eq(data.filenameType, 'string', `${scriptName} sets __filename`);
    assert.eq(data.dirnameType, 'string', `${scriptName} sets __dirname`);
    assert.ok(data.dirnameMatches, `${scriptName} keeps dirname aligned`);
    assert.eq(data.basename, scriptName, `${scriptName} keeps the right basename`);
}

await testEntryScript('log-entry-filename-dirname.js');
await testEntryScript('log-entry-filename-dirname.mjs');
