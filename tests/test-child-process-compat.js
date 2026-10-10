import assert from 'node:assert';
import { exec, spawn, spawnSync } from 'node:child_process';

async function testExecReturnsStdout() {
    await new Promise((resolve, reject) => {
        exec('echo hi', (error, stdout, stderr) => {
            try {
                assert.equal(error, null);
                assert.equal(stdout, 'hi\n');
                assert.equal(stderr, '');
                resolve();
            } catch (err) {
                reject(err);
            }
        });
    });
}

async function testSpawnEnoentEmitsError() {
    let spawnThrew = false;
    let child = null;
    try {
        child = spawn('definitely-not-found-cmd-xyz', []);
    } catch {
        spawnThrew = true;
    }
    assert.equal(spawnThrew, false, 'spawn should not throw for ENOENT');

    await new Promise((resolve, reject) => {
        let gotError = false;
        let gotClose = false;

        child.on('error', (err) => {
            gotError = true;
            try {
                assert.ok(err);
            } catch (e) {
                reject(e);
            }
        });

        child.on('close', () => {
            gotClose = true;
            if (gotError) {
                resolve();
            }
        });

        setTimeout(() => {
            if (!gotError || !gotClose) {
                reject(new Error('spawn ENOENT should emit both error and close'));
            }
        }, 500);
    });
}

function testSpawnSyncNodeShape() {
    const ret = spawnSync('echo', [ 'sync-ok' ]);

    assert.equal(typeof ret, 'object');
    assert.equal(typeof ret.then, 'undefined', 'spawnSync should return plain object, not Promise');
    assert.equal(ret.status, 0);
    assert.equal(ret.signal, null);
    assert.ok(Buffer.isBuffer(ret.stdout), 'default stdout should be Buffer');
    assert.ok(Buffer.isBuffer(ret.stderr), 'default stderr should be Buffer');
    assert.equal(ret.stdout.toString(), 'sync-ok\n');
    assert.ok(Array.isArray(ret.output));
    assert.equal(ret.output.length, 3);
    assert.equal(ret.output[0], null);
    assert.ok(Buffer.isBuffer(ret.output[1]));
    assert.ok(Buffer.isBuffer(ret.output[2]));
}

function testSpawnSyncEncoding() {
    const ret = spawnSync('echo', [ 'utf8-ok' ], { encoding: 'utf8' });

    assert.equal(ret.status, 0);
    assert.equal(typeof ret.stdout, 'string');
    assert.equal(typeof ret.stderr, 'string');
    assert.equal(ret.stdout, 'utf8-ok\n');
    assert.equal(ret.stderr, '');
    assert.equal(typeof ret.output[1], 'string');
    assert.equal(typeof ret.output[2], 'string');
}

await testExecReturnsStdout();
await testSpawnEnoentEmitsError();
testSpawnSyncNodeShape();
testSpawnSyncEncoding();

console.log('child_process compat tests passed');
