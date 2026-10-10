import assert from 'tjs:assert';

const http = require('node:http');
const decoder = new TextDecoder();

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

const expectedHost = 'proxy-target.example';
let seenHost = null;
let seenUrl = null;

const server = http.createServer((req, res) => {
    seenHost = req.headers.host;
    seenUrl = req.url;

    res.writeHead(200, { 'x-seen-host': seenHost });
    res.end(req.url);
});

await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));

const addr = server.address();

const body = await new Promise((resolve, reject) => {
    const req = http.request({
        hostname: '127.0.0.1',
        port: addr.port,
        path: '/api/check?x=1',
        headers: { Host: expectedHost },
    }, (res) => {
        assert.eq(res.statusCode, 200);
        assert.eq(res.headers['x-seen-host'], expectedHost);

        const chunks = [];
        res.on('data', (chunk) => chunks.push(chunk));
        res.on('end', () => resolve(decoder.decode(concatChunks(chunks))));
    });

    req.on('error', reject);
    req.end();
});

assert.eq(body, '/api/check?x=1');
assert.eq(seenUrl, '/api/check?x=1');
assert.eq(seenHost, expectedHost);

await new Promise((resolve) => server.close(resolve));
