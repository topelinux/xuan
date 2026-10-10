/**
 * Local Node.js HTTP/2 server fixture for testing xuan http2 client.
 * Run with: node tests/test-http2-unary-fixture.cjs
 */
const http2 = require('http2');
const fs = require('fs');
const path = require('path');

const PORT = 19083;

// Generate self-signed cert on the fly
function generateSelfSignedCert() {
    const { execSync } = require('child_process');
    const certPath = path.join(__dirname, '_test_cert.pem');
    const keyPath = path.join(__dirname, '_test_key.pem');

    if (fs.existsSync(certPath) && fs.existsSync(keyPath)) {
        return { cert: fs.readFileSync(certPath), key: fs.readFileSync(keyPath) };
    }

    execSync(`openssl req -x509 -newkey rsa:2048 -keyout ${keyPath} -out ${certPath} -days 1 -nodes -subj "/CN=localhost"`, {
        stdio: 'pipe'
    });

    return { cert: fs.readFileSync(certPath), key: fs.readFileSync(keyPath) };
}

const { cert, key } = generateSelfSignedCert();

const server = http2.createSecureServer({
    cert,
    key,
    allowHTTP1: false
});

server.on('session', (session) => {
    console.log('server-session established');
    session.on('error', (err) => {
        console.error('server-sessionError', err.message || err);
    });
    session.on('close', () => {
        console.log('server-session closed');
    });
});

server.on('stream', (stream, headers) => {
    const method = headers[':method'];
    const urlPath = headers[':path'];
    console.log(`server-stream ${method} ${urlPath}`);

    // Log all headers for debugging
    const hdrs = {};
    for (const [k, v] of Object.entries(headers)) {
        if (!k.startsWith(':')) {
            hdrs[k] = v;
        }
    }
    if (Object.keys(hdrs).length > 0) {
        console.log('server-headers', JSON.stringify(hdrs));
    }

    let body = [];
    let ended = false;
    stream.on('data', (chunk) => {
        body.push(chunk);
        console.log('server-data', chunk.length, 'bytes');
    });
    stream.on('end', () => {
        ended = true;
        const bodyData = Buffer.concat(body);
        console.log('server-end body=', bodyData.length, 'bytes');

        // Respond with a simple gRPC-like response
        const responseData = Buffer.from([0, 0, 0, 0, 0]); // 5-byte empty gRPC message
        stream.respond({
            ':status': 200,
            'content-type': 'application/grpc',
        });
        stream.end(responseData);
        console.log('server-response sent');
    });
    stream.on('close', () => {
        if (!ended) {
            console.log('server-stream closed without end (body so far:', body.length, 'chunks)');
        } else {
            console.log('server-stream closed normally');
        }
    });
    stream.on('error', (err) => {
        console.error('server-streamError', err.message || err);
    });
});

server.listen(PORT, () => {
    console.log(`H2 server listening on https://localhost:${PORT}`);
    console.log('Waiting for xuan client connection...');
});

// Graceful shutdown
process.on('SIGINT', () => {
    console.log('\nShutting down server...');
    server.close();
    process.exit(0);
});

// Auto-timeout after 30s
setTimeout(() => {
    console.log('Server timeout (30s), shutting down...');
    server.close();
    process.exit(0);
}, 30000);
