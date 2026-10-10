import assert from 'tjs:assert';
import { spawn } from 'node:child_process';

const decoder = new TextDecoder();

async function testSpawnDefaultStdioIsPipe() {
    const proc = spawn('echo', [ 'hello-default-stdio' ]);

    assert.ok(proc.stdin !== null, 'stdin defaults to pipe');
    assert.ok(proc.stdout !== null, 'stdout defaults to pipe');
    assert.ok(proc.stderr !== null, 'stderr defaults to pipe');
    assert.eq(typeof proc.stdout.on, 'function', 'stdout is an event emitter-like stream');

    let output = '';
    const endPromise = new Promise((resolve, reject) => {
        proc.stdout.on('data', chunk => {
            assert.ok(Buffer.isBuffer(chunk), 'stdout data event emits Buffer chunks');
            output += decoder.decode(chunk, { stream: true });
        });
        proc.stdout.on('end', () => {
            output += decoder.decode();
            resolve();
        });
        proc.stdout.on('error', reject);
    });

    const status = await proc.wait();
    await endPromise;

    assert.eq(status.exit_status, 0, 'spawn exits successfully');
    assert.ok(output.includes('hello-default-stdio'), 'receives stdout data via data event');
}

await testSpawnDefaultStdioIsPipe();
