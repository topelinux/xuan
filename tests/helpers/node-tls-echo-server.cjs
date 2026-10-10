'use strict';

const fs = require('node:fs');
const tls = require('node:tls');

const keyPath = process.argv[2];
const certPath = process.argv[3];

if (!keyPath || !certPath) {
    console.error('Usage: node node-tls-echo-server.cjs <key.pem> <cert.pem>');
    process.exit(2);
}

const server = tls.createServer({
    key: fs.readFileSync(keyPath),
    cert: fs.readFileSync(certPath),
}, (socket) => {
    socket.on('error', (err) => {
        console.error('SOCKET_ERROR:' + (err && err.stack ? err.stack : String(err)));
    });
    socket.pipe(socket);
});

server.on('error', (err) => {
    console.error('SERVER_ERROR:' + (err && err.stack ? err.stack : String(err)));
    process.exit(1);
});

server.listen(0, '127.0.0.1', () => {
    const addr = server.address();
    console.log('PORT:' + addr.port);
});

process.on('SIGTERM', () => {
    server.close(() => process.exit(0));
    setTimeout(() => process.exit(1), 2000).unref();
});
