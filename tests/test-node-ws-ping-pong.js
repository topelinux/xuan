import assert from 'tjs:assert';
import { Buffer } from 'tjs:buffer';
import crypto from 'node:crypto';
import net from 'node:net';

const WS = require('node:ws');
const GUID = '258EAFA5-E914-47DA-95CA-C5AB0DC85B11';

function acceptKey(key) {
    return crypto.createHash('sha1').update(`${key}${GUID}`).digest('base64');
}

function decodeClientFrame(buffer) {
    const opcode = buffer[0] & 0x0f;
    const masked = (buffer[1] & 0x80) !== 0;
    const length = buffer[1] & 0x7f;
    assert.ok(masked, 'client control frame is masked');
    const mask = buffer.subarray(2, 6);
    const payload = Buffer.alloc(length);
    for (let i = 0; i < length; i++) {
        payload[i] = buffer[6 + i] ^ mask[i % 4];
    }
    return { opcode, payload };
}

const server = net.createServer();
await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));

const { port } = server.address();
const pongPayload = new Promise((resolve, reject) => {
    const timer = setTimeout(() => reject(new Error('timed out waiting for pong')), 2000);

    server.once('connection', (socket) => {
        let handshook = false;
        let pending = Buffer.alloc(0);

        socket.on('data', (chunk) => {
            pending = Buffer.concat([ pending, chunk ]);

            if (!handshook) {
                const request = pending.toString();
                const headerEnd = request.indexOf('\r\n\r\n');
                if (headerEnd === -1) {
                    return;
                }

                const keyLine = request.split('\r\n').find((line) => line.toLowerCase().startsWith('sec-websocket-key:'));
                const key = keyLine.split(':')[1].trim();
                socket.write([
                    'HTTP/1.1 101 Switching Protocols',
                    'Upgrade: websocket',
                    'Connection: Upgrade',
                    `Sec-WebSocket-Accept: ${acceptKey(key)}`,
                    '\r\n',
                ].join('\r\n'));

                handshook = true;
                pending = pending.subarray(headerEnd + 4);
                socket.write(Buffer.from([ 0x89, 5, 0x61, 0x6c, 0x69, 0x76, 0x65 ]));
                return;
            }

            if (pending.length >= 11) {
                clearTimeout(timer);
                resolve(decodeClientFrame(pending));
            }
        });
    });
});

const client = new WS(`ws://127.0.0.1:${port}`);
const frame = await pongPayload;

assert.eq(frame.opcode, 0x0a, 'client replies with pong opcode');
assert.eq(frame.payload.toString(), 'alive', 'client replies pong with ping payload');

client.close();
server.close();

const wss = new WS.Server({ port: 0 });
await new Promise((resolve) => wss.once('listening', resolve));

const wsPort = wss.address().port;
const wsClient = new WS(`ws://127.0.0.1:${wsPort}`);

const serverPong = await new Promise((resolve, reject) => {
    const timer = setTimeout(() => reject(new Error('timed out waiting for server pong event')), 2000);
    wss.once('connection', (socket) => {
        socket.once('pong', (data) => {
            clearTimeout(timer);
            resolve(data);
        });
        socket.ping('server');
    });
    wsClient.once('error', reject);
});

assert.eq(Buffer.from(serverPong).toString(), 'server', 'node:ws server receives automatic client pong');

wsClient.close();
await new Promise((resolve) => wss.close(resolve));
