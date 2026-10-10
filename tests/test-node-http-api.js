import assert from 'tjs:assert';

import httpImport from 'node:http';

const httpRequireNs = require('node:http');
const httpRequire = httpRequireNs.default ?? httpRequireNs;

assert.eq(typeof httpImport.createServer, 'function');
assert.eq(typeof httpImport.request, 'function');
assert.eq(typeof httpImport.get, 'function');
assert.eq(typeof httpImport.Server, 'function');
assert.eq(typeof httpRequire.createServer, 'function');

const server = httpImport.createServer();
assert.eq(typeof server.listen, 'function');
assert.eq(typeof server.close, 'function');
assert.eq(typeof server.address, 'function');
assert.eq(server.address(), null);

// close() should still invoke callback when not listening.
await new Promise((resolve) => server.close(resolve));

const originalFetch = globalThis.fetch;
const encoder = new TextEncoder();
let lastFetch = null;

globalThis.fetch = async (url, options) => {
    lastFetch = { url: String(url), options };
    return new Response(encoder.encode('mock-body'), {
        status: 202,
        headers: { 'x-unit': 'ok' },
    });
};

const body = await new Promise((resolve, reject) => {
    const req = httpImport.request('http://unit.test/abc', { method: 'POST' }, (res) => {
        assert.eq(res.statusCode, 202);
        assert.eq(res.headers['x-unit'], 'ok');

        const chunks = [];
        res.on('data', (chunk) => chunks.push(chunk));
        res.on('end', () => {
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
            resolve(new TextDecoder().decode(out));
        });
    });

    req.on('error', reject);
    req.setHeader('x-req', 'yes');
    req.write('payload');
    req.end();
});

assert.eq(body, 'mock-body');
assert.ok(lastFetch != null);
assert.eq(lastFetch.url, 'http://unit.test/abc');
assert.eq(lastFetch.options.method, 'POST');
assert.eq(lastFetch.options.headers['x-req'], 'yes');

globalThis.fetch = originalFetch;
