import assert from 'tjs:assert';

const http = require('node:http');

function concatChunks(chunks) {
    let total = 0;
    for (const chunk of chunks) {
        total += chunk.byteLength;
    }
    const out = new Uint8Array(total);
    let offset = 0;
    for (const chunk of chunks) {
        out.set(chunk, offset);
        offset += chunk.byteLength;
    }
    return out;
}

const decoder = new TextDecoder();

const server = http.createServer((req, res) => {
    const chunks = [];
    req.on('data', (chunk) => chunks.push(chunk));
    req.on('end', () => {
        const body = decoder.decode(concatChunks(chunks));
        assert.eq(req.method, 'POST');
        assert.eq(req.url, '/echo?x=1');

        res.setHeader('x-echo', 'ok');
        res.writeHead(201);
        res.end(`echo:${body}`);
    });
});

await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));

const addr = server.address();
assert.eq(typeof addr.port, 'number');
assert.ok(addr.port > 0);

const resp = await fetch(`http://127.0.0.1:${addr.port}/echo?x=1`, {
    method: 'POST',
    body: 'hello-http',
});

assert.eq(resp.status, 201);
assert.eq(resp.headers.get('x-echo'), 'ok');
assert.eq(await resp.text(), 'echo:hello-http');

await new Promise((resolve) => server.close(resolve));
