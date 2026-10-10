import assert from 'tjs:assert';

const WS = require('node:ws');

const wss = new WS.Server({ port: 0, path: '/ws' });

console.log('in node-ws-server');
async function main() {
    wss.on('connection', (socket, req) => {
        assert.eq(req.url, '/ws', 'request path matches');
        socket.on('message', (data, isBinary) => {
            console.log('in on onmessage', data);
            if (isBinary) {
                socket.send(data);
            } else {
                socket.send(`echo:${data}`);
            }
        });
    });

    await new Promise((resolve) => wss.once('listening', resolve));

    const { port } = wss.address();
    const ws = new WebSocket(`ws://127.0.0.1:${port}/ws`);

    const message = await new Promise((resolve, reject) => {
        ws.onopen = () => ws.send('hello');
        ws.onmessage = (event) => {
            console.log('data', event.data);
            resolve(event.data);
        }
        ws.onerror = () => reject(new Error('ws client error'));
    });

    assert.eq(message, 'echo:hello', 'node:ws server echoes data');

    ws.close();
    await new Promise((resolve) => wss.close(resolve));
}
main();
