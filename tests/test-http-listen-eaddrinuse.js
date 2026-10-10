import assert from 'tjs:assert';
import http from 'node:http';
import net from 'node:net';

const core = globalThis[Symbol.for('tjs.internal.core')];

function waitForListening(server) {
    return new Promise((resolve, reject) => {
        server.once('error', reject);
        server.listen(0, '127.0.0.1', resolve);
    });
}

function waitForServerError(server, start) {
    return new Promise((resolve, reject) => {
        const timer = setTimeout(() => reject(new Error('timed out waiting for listen error')), 1000);
        server.once('error', (err) => {
            clearTimeout(timer);
            resolve(err);
        });
        start();
    });
}

function closeServer(server) {
    return new Promise((resolve) => server.close(resolve));
}

function assertListenAddrInUse(err, port) {
    assert.eq(err.code, 'EADDRINUSE');
    assert.eq(err.errno, core.UV_EADDRINUSE);
    assert.eq(err.syscall, 'listen');
    assert.eq(err.address, '127.0.0.1');
    assert.eq(err.port, port);
    const json = JSON.parse(JSON.stringify(err));
    assert.eq(json, {
        code: 'EADDRINUSE',
        errno: core.UV_EADDRINUSE,
        syscall: 'listen',
        address: '127.0.0.1',
        port,
    });
}

const blocker = net.createServer();
await waitForListening(blocker);
const port = blocker.address().port;

try {
    const handledServer = http.createServer((_req, res) => res.end('ok'));
    const handledErr = await waitForServerError(handledServer, () => {
        handledServer.listen(port, '127.0.0.1');
    });
    assertListenAddrInUse(handledErr, port);

    const uncaughtErr = await new Promise((resolve, reject) => {
        const timer = setTimeout(() => reject(new Error('timed out waiting for uncaughtException')), 1000);
        process.once('uncaughtException', (err) => {
            clearTimeout(timer);
            resolve(err);
        });
        http.createServer((_req, res) => res.end('ok')).listen(port, '127.0.0.1');
    });
    assertListenAddrInUse(uncaughtErr, port);
} finally {
    await closeServer(blocker);
}
