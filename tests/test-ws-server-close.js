import assert from 'tjs:assert';


// Close callback fires when client disconnects.
let closeFired = false;
const closed = new Promise((resolve) => {
    closeFired = resolve;
});

const server = tjs.serve({
    port: 0,
    fetch(req, { server }) {
        if (server.upgrade(req)) {
            return;
        }

        return new Response('not ws');
    },
    websocket: {
        message() {},
        close(ws, code, reason) {
            closeFired({ code, reason });
        },
    },
});

const ws = new WebSocket(`ws://127.0.0.1:${server.port}`);

await new Promise((resolve) => {
    ws.onopen = () => {
        ws.close(1000, 'client done');
        resolve();
    };
});

const result = await closed;
assert.eq(result.code, 1000, 'server receives client close code');
assert.eq(result.reason, 'client done', 'server receives client close reason');
server.close();
