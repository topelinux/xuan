import assert from 'tjs:assert';
import fs from 'node:fs';
import { spawn } from 'node:child_process';

const platform = navigator.userAgentData.platform;

function delay(ms) {
    return new Promise(resolve => setTimeout(resolve, ms));
}

function countOpenFds() {
    return fs.readdirSync('/dev/fd').length;
}

async function runNodeChildProcessSpawn() {
    await new Promise((resolve, reject) => {
        const child = spawn('true', [], { stdio: [ 'pipe', 'ignore', 'ignore' ] });

        child.on('error', reject);
        child.on('close', resolve);
    });
    await delay(20);
}

async function runTjsSpawn() {
    const proc = tjs.spawn([ 'true' ], { stdin: 'pipe', stdout: 'ignore', stderr: 'ignore' });

    await proc.wait();
    await delay(20);
}

async function assertFdStable(label, run) {
    await run();
    const baseline = countOpenFds();

    for (let i = 0; i < 12; i++) {
        await run();
    }

    const after = countOpenFds();
    assert.ok(after <= baseline + 2, `${label} leaked fds: baseline=${baseline} after=${after}`);
}

let canCountFds = true;

if (platform === 'Windows') {
    canCountFds = false;
    console.log('skip test-child-process-stdio-fd-leak: /dev/fd is not available on Windows');
} else {
    try {
        countOpenFds();
    } catch (error) {
        canCountFds = false;
        console.log(`skip test-child-process-stdio-fd-leak: ${error.message}`);
    }
}

if (canCountFds) {
    await assertFdStable('node:child_process stdin pipe', runNodeChildProcessSpawn);
    await assertFdStable('tjs.spawn stdin pipe', runTjsSpawn);
}
