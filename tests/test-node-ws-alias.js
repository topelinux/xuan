import assert from 'tjs:assert';

const WebSocket = require('ws');

const server = tjs.serve({
    port: 0,
    fetch(req, { server }) {
        if (server.upgrade(req)) {
            return;
        }
        return new Response('bad websocket upgrade', { status: 400 });
    },
    websocket: {
        message(ws, data) {
            ws.sendText(`echo:${data}`);
        },
    },
});

const ws = new WebSocket(`ws://127.0.0.1:${server.port}`);

const message = await new Promise((resolve, reject) => {
    ws.on('open', () => ws.send('hello'));
    ws.on('message', (data) => resolve(data.toString()));
    ws.on('error', reject);
});

assert.eq(message, 'echo:hello', 'bare ws alias client works');

ws.close();
server.close();
