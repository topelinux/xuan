import assert from 'tjs:assert';

const http = require('node:http');
const net = require('node:net');

function listen(server, port = 0) {
    return new Promise((resolve, reject) => {
        server.once('error', reject);
        server.listen(port, '127.0.0.1', () => {
            server.off('error', reject);
            resolve(server.address().port);
        });
    });
}

function close(server) {
    return new Promise((resolve) => server.close(resolve));
}

async function openIdleTcp(port) {
    const socket = net.createConnection({ host: '127.0.0.1', port });
    socket.on('error', () => undefined);
    await new Promise((resolve) => socket.once('connect', resolve));
    return socket;
}

const first = http.createServer((_req, res) => res.end('ok'));
const port = await listen(first);
const idleSocket = await openIdleTcp(port);
await close(first);
idleSocket.destroy();

const second = http.createServer((_req, res) => res.end('ok'));
const reboundPort = await listen(second, port);
assert.eq(reboundPort, port);
await close(second);
