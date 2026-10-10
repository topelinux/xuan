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
            if (done) {
                return;
            }
            done = true;
            clearTimeout(timer);
            proc.stdout?.off('data', onStdout);
            proc.stderr?.off('data', onStderr);
            proc.off('error', onProcError);
            if (err) {
                reject(err);
            } else {
                resolve(port);
            }
        };

        const onStdout = (chunk) => {
            stdoutBuf += decoder.decode(chunk, { stream: true });
            const m = stdoutBuf.match(/PORT:(\d+)/);
            if (m) {
                finish(null, Number(m[1]));
            }
        };

        const onStderr = (chunk) => {
            stderrBuf += decoder.decode(chunk, { stream: true });
        };

        const onProcError = (err) => {
            finish(new Error(`failed to start node tls server: ${err.message}`));
        };

        const timer = setTimeout(() => {
            finish(new Error(`timeout waiting server port; stdout=${stdoutBuf} stderr=${stderrBuf}`));
        }, timeoutMs);

        proc.stdout.on('data', onStdout);
        proc.stderr.on('data', onStderr);
        proc.on('error', onProcError);
    });
}

async function runClient(port) {
    const payload = 'ping-over-tls';
    const socket = tls.connect({
        host: '127.0.0.1',
        port,
        rejectUnauthorized: false,
    });

    let recv = '';

    await new Promise((resolve, reject) => {
        const timer = setTimeout(() => {
            reject(new Error('timeout waiting tls echo response'));
        }, 5000);

        socket.on('error', (err) => {
            clearTimeout(timer);
            reject(err);
        });

        socket.on('secureConnect', () => {
            socket.write(payload);
        });

        socket.on('data', (chunk) => {
            recv += decoder.decode(chunk, { stream: true });
            if (recv.length >= payload.length) {
                socket.end();
                clearTimeout(timer);
                resolve();
            }
        });
    });

    assert.eq(recv, payload);
    assert.eq(socket.encrypted, true);
}

const helperPath = path.join(import.meta.dirname, 'helpers', 'node-tls-echo-server.cjs');
const keyPath = path.join(import.meta.dirname, 'fixtures', 'tls', 'key.pem');
const certPath = path.join(import.meta.dirname, 'fixtures', 'tls', 'cert.pem');

const proc = spawn('node', [ helperPath, keyPath, certPath ]);

assert.ok(proc.stdout !== null);
assert.ok(proc.stderr !== null);

try {
    const port = await waitForPort(proc);
    assert.ok(port > 0);
    await runClient(port);
} finally {
    proc.kill('SIGTERM');
    const status = await proc.wait();
    assert.ok(status.exit_status === 0 || status.exit_status === 1 || status.term_signal === 'SIGTERM');
}
