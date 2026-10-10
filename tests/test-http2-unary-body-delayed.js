/**
 * xuan HTTP/2 client test — unary POST with body data (delayed write).
 * Usage: ./build/xuan tests/test-http2-unary-body-delayed.js
 */
const http2 = require('http2');

const TARGET = 'https://localhost:19083';

console.log('xuan http2 client (delayed body test): connecting to', TARGET);

const session = http2.connect(TARGET, {
    rejectUnauthorized: false,
});

session.on('connect', () => {
    console.log('client: session connected');
});

session.on('error', (err) => {
    console.error('client: session error', err);
});

session.on('close', () => {
    console.log('client: session closed');
    process.exit(0);
});

setTimeout(() => {
    console.log('client: sending POST with body (endStream:false)...');

    const headers = {
        ':method': 'POST',
        ':path': '/svc/Unary',
        ':scheme': 'https',
        'content-type': 'application/grpc',
        'te': 'trailers',
    };

    const stream = session.request(headers, { endStream: false });

    stream.on('response', (responseHeaders) => {
        console.log('client: response', JSON.stringify(responseHeaders));
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

    // Delay write/end to let the connection establish first
    setTimeout(() => {
        console.log('client: writing body and ending stream...');
        const body = new Uint8Array([0, 0, 0, 0, 0]);
        stream.write(body);
        stream.end();
    }, 500);
}, 200);

setTimeout(() => {
    console.error('client: timeout after 15s');
    session.close();
    process.exit(1);
}, 15000);
