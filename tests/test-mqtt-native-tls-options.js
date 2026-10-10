import assert from 'tjs:assert';
import fs from 'node:fs';
import http from 'node:http';
import path from 'node:path';

const core = globalThis[Symbol.for('tjs.internal.core')];
const fixtureDir = path.join(import.meta.dirname, 'fixtures', 'tls');
const cert = fs.readFileSync(path.join(fixtureDir, 'cert.pem'));
const key = fs.readFileSync(path.join(fixtureDir, 'key.pem'));
const chain = Buffer.concat([ cert, Buffer.from('\n'), cert ]);

const server = http.createServer((_req, res) => res.end('ok'));
await new Promise((resolve, reject) => {
    server.once('error', reject);
    server.listen(0, '127.0.0.1', resolve);
});

let client;
const tlsConfigErrors = [];

try {
    client = new core.MQTTClient({
        protocol: 'mqtts',
        host: '127.0.0.1',
        port: server.address().port,
        ca: [ cert ],
        cert: chain,
        key,
    });

    await new Promise((resolve) => {
        let done = false;
        const finish = () => {
            if (done) {
                return;
            }
            done = true;
            clearTimeout(timer);
            resolve();
        };

        client.onerror = (error) => {
            const message = error instanceof Error ? error.message : String(error || '');
            if (message.includes('CA parse failed') ||
                message.includes('certificate parse failed') ||
                message.includes('key parse failed') ||
                message.includes('failed to configure MQTT TLS')) {
                tlsConfigErrors.push(message);
            }
        };
        client.onclose = finish;

        const timer = setTimeout(() => {
            client.close();
            finish();
        }, 1000);
    });
} finally {
    client?.close();
    await new Promise((resolve) => server.close(resolve));
}

assert.eq(tlsConfigErrors.length, 0);
