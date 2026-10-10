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

function readBody(req) {
    return new Promise((resolve, reject) => {
        const chunks = [];

        req.on('data', (chunk) => chunks.push(chunk));
        req.on('end', () => resolve(decoder.decode(concatChunks(chunks))));
        req.on('error', reject);
    });
}

const payload = JSON.stringify({
    enable: false,
    protocol: 'mqtts',
    host: 'mqtt.example.com',
    port: 8883,
});
const expectedLength = String(new TextEncoder().encode(payload).byteLength);

let backendBody = null;
let backendLength = null;

const backend = http.createServer(async (req, res) => {
    backendLength = req.headers['content-length'];
    backendBody = await readBody(req);

    res.writeHead(200, {
        'content-type': 'application/json',
        'x-backend-length': backendLength,
    });
    res.end(JSON.stringify({ ok: true, body: backendBody }));
});

await new Promise((resolve) => backend.listen(0, '127.0.0.1', resolve));

const backendAddress = backend.address();

const frontend = http.createServer((req, res) => {
    const proxyReq = http.request({
        hostname: '127.0.0.1',
        port: backendAddress.port,
        method: req.method,
        path: req.url,
        headers: {
            ...req.headers,
            host: `127.0.0.1:${backendAddress.port}`,
        },
    }, (proxyRes) => {
        res.writeHead(proxyRes.statusCode || 502, proxyRes.headers);
        proxyRes.pipe(res);
    });

    proxyReq.on('error', (err) => {
        res.writeHead(502, { 'content-type': 'text/plain' });
        res.end(err.message);
    });

    req.pipe(proxyReq);
});

await new Promise((resolve) => frontend.listen(0, '127.0.0.1', resolve));

const frontendAddress = frontend.address();
const resp = await fetch(`http://127.0.0.1:${frontendAddress.port}/api/cloud/config`, {
    method: 'PUT',
    headers: { 'content-type': 'application/json' },
    body: payload,
});

const respBody = await resp.json();

assert.eq(resp.status, 200);
assert.eq(resp.headers.get('x-backend-length'), expectedLength);
assert.eq(respBody.ok, true);
assert.eq(respBody.body, payload);
assert.eq(backendBody, payload);
assert.eq(backendLength, expectedLength);
assert.eq(backendLength.includes(','), false);

await new Promise((resolve) => frontend.close(resolve));
await new Promise((resolve) => backend.close(resolve));
