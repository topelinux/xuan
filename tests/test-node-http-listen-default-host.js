import assert from 'tjs:assert';
import http from 'node:http';

const server = http.createServer((_req, res) => res.end('ok'));
await new Promise((resolve, reject) => {
    server.once('error', reject);
    server.listen(0, resolve);
});

const addr = server.address();
assert.ok(addr !== null);
assert.ok(addr.family === 'IPv4' || addr.family === 'IPv6');

await new Promise((resolve) => server.close(resolve));
