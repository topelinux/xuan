import assert from 'tjs:assert';


// Server-initiated close.
let serverCloseFired = false;
const serverClosed = new Promise((resolve) => {
    serverCloseFired = resolve;
});

function shouldSkipServerTest(error) {
    const msg = String(error && error.message ? error.message : error);
    return msg.includes('EPERM')
        || msg.includes('EACCES')
        || msg.includes('operation not permitted')
        || msg.includes('failed to create HTTP server vhost');
}

let server;
try {
    server = tjs.serve({
        port: 0,
        fetch(req, { server }) {
            if (server.upgrade(req)) {
                return;
            }

            return new Response('not ws');
        },
        websocket: {
            open(ws) {
                ws.close(4001, 'server bye');
            },
            message() {},
            close() {
                serverCloseFired(true);
            },
        },
    });
} catch (error) {
    if (shouldSkipServerTest(error)) {
        console.log('skip test-ws-server-server-close:', String(error.message || error));
        process.exit(0);
    }
    throw error;
}

const ws = new WebSocket(`ws://127.0.0.1:${server.port}`);

await new Promise((resolve) => {
    ws.onclose = resolve;
});

const result = await serverClosed;
assert.ok(result, 'server close callback fired');
server.close();
