import assert from 'tjs:assert';
import { spawnSync } from 'node:child_process';

// Test 1: Abort before response (during fetch)
async function testAbortBeforeResponse() {
    const controller = new AbortController();

    // Abort immediately
    setTimeout(() => controller.abort(), 50);

    let threw = false;
    let errorName = null;

    try {
        await fetch('https://postman-echo.com/delay/3', {
            signal: controller.signal
        });
    } catch (e) {
        threw = true;
        errorName = e.name;
    }

    assert.ok(threw, 'Should have thrown when aborting before response');
    assert.eq(errorName, 'AbortError', 'throws AbortError');
}

// Test 2: Abort signal already aborted
async function testAlreadyAborted() {
    const script = `
        const controller = new AbortController();
        controller.abort();
        await fetch('https://postman-echo.com/get', { signal: controller.signal });
    `;
    const ret = spawnSync(process.execPath, [ 'eval', script ], { encoding: 'utf8' });
    assert.eq(ret.exit_status ?? ret.status, 1);
    assert.ok(ret.stderr.includes('AbortError'), 'throws AbortError');
}

await testAbortBeforeResponse();
await testAlreadyAborted();
