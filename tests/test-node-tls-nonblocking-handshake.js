import assert from 'tjs:assert';
import path from 'node:path';
import tls from 'node:tls';
import { spawn } from 'node:child_process';

const decoder = new TextDecoder();

function waitForPort(proc, timeoutMs = 5000) {
    return new Promise((resolve, reject) => {
        let stdoutBuf = '';
        let stderrBuf = '';
        let done = false;

        const finish = (err, port) => {
            if (done) return;
            done = true;
            clearTimeout(timer);
            proc.stdout?.off('data', onStdout);
            proc.stderr?.off('data', onStderr);
            proc.off('error', onProcError);
            if (err) reject(err);
            else resolve(port);
        };

        const onStdout = (chunk) => {
            stdoutBuf += decoder.decode(chunk, { stream: true });
            const m = stdoutBuf.match(/PORT:(\d+)/);
            if (m) finish(null, Number(m[1]));
        };
        const onStderr = (chunk) => {
            stderrBuf += decoder.decode(chunk, { stream: true });
        };
        const onProcError = (err) => {
            finish(new Error(`failed to start stall server: ${err.message}`));
        };

        const timer = setTimeout(() => {
            finish(new Error(`timeout waiting server port; stdout=${stdoutBuf} stderr=${stderrBuf}`));
        }, timeoutMs);

        proc.stdout.on('data', onStdout);
        proc.stderr.on('data', onStderr);
        proc.on('error', onProcError);
    });
}

const helperPath = path.join(import.meta.dirname, 'helpers', 'tcp-stall-server.cjs');
const proc = spawn('node', [ helperPath ]);

assert.ok(proc.stdout !== null);
assert.ok(proc.stderr !== null);

let socket;
try {
    const port = await waitForPort(proc);
    socket = tls.connect({
        host: '127.0.0.1',
        port,
        rejectUnauthorized: false,
    });
    socket.on('error', () => {});

    let timerFired = false;
    await new Promise((resolve, reject) => {
        const watchdog = setTimeout(() => reject(new Error('timer did not fire while TLS handshake was pending')), 3000);
        setTimeout(() => {
            timerFired = true;
            clearTimeout(watchdog);
            resolve();
        }, 200);
    });

    assert.eq(timerFired, true);
} finally {
    socket?.destroy();
    proc.kill('SIGTERM');
    await proc.wait().catch(() => undefined);
}
