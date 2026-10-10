import assert from 'tjs:assert';
import http from 'node:http';

const WebSocket = require('ws');

const server = http.createServer((_req, res) => {
    res.statusCode = 404;
    res.end('not found');
});
const wss = new WebSocket.Server({ noServer: true });

wss.on('connection', (ws, req) => {
    assert.eq(req.url, '/ws');
    ws.on('message', (message) => ws.send(`echo:${message}`));
});

server.on('upgrade', (req, socket, head) => {
    if (req.url === '/ws') {
        wss.handleUpgrade(req, socket, head, (ws) => {
            wss.emit('connection', ws, req);
        });
        return;
    }
    socket.end('HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n');
});

await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
const { port } = server.address();
const ws = new WebSocket(`ws://127.0.0.1:${port}/ws`);

const message = await new Promise((resolve, reject) => {
    ws.on('open', () => ws.send('hello'));
    ws.on('message', (data) => resolve(data.toString()));
    ws.on('error', reject);
});

assert.eq(message, 'echo:hello');

ws.close();
wss.close();
await new Promise((resolve) => server.close(resolve));
