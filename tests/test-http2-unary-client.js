/**
 * xuan HTTP/2 client test — unary request to local Node.js H2 server.
 * Run after starting the server: node tests/test-http2-unary-fixture.js
 *
 * Usage: ./build/xuan tests/test-http2-unary-client.js
 */
const http2 = require('http2');

const TARGET = 'https://localhost:19083';

console.log('xuan http2 client: connecting to', TARGET);

const session = http2.connect(TARGET, {
    rejectUnauthorized: false,
});

session.on('connect', () => {
    console.log('client: session connected');
});

session.on('remoteSettings', (settings) => {
    console.log('client: remoteSettings', JSON.stringify(settings));
});

session.on('error', (err) => {
    console.error('client: session error', err);
});

session.on('close', () => {
    console.log('client: session closed');
    process.exit(0);
});

// Wait a tick for connection to establish
setTimeout(() => {
    console.log('client: sending request...');

    const headers = {
        ':method': 'POST',
        ':path': '/svc/Unary',
        ':scheme': 'https',
        'content-type': 'application/grpc',
        'te': 'trailers',
    };

    const stream = session.request(headers, { endStream: true });

    stream.on('response', (responseHeaders, flags) => {
        console.log('client: response headers', JSON.stringify(responseHeaders), 'flags=', flags);
    });

    stream.on('data', (chunk) => {
        console.log('client: data', chunk.length, 'bytes');
    });

    stream.on('end', () => {
        console.log('client: stream ended');
    });

    stream.on('close', () => {
        console.log('client: stream closed');
        session.close();
    });

    stream.on('error', (err) => {
        console.error('client: stream error', err);
    });

    console.log('client: stream id =', stream.id);
    console.log('client: sentHeaders =', JSON.stringify(stream.sentHeaders));
}, 200);

// Timeout after 15s
setTimeout(() => {
    console.error('client: timeout after 15s');
    session.close();
    process.exit(1);
}, 15000);
