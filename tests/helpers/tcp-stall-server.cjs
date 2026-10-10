'use strict';

const net = require('node:net');

const sockets = new Set();
const server = net.createServer((socket) => {
    sockets.add(socket);
    socket.on('close', () => sockets.delete(socket));
    socket.on('error', () => {});
    // Intentionally keep the socket open and send no TLS handshake bytes.
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
    for (const socket of sockets) {
        socket.destroy();
    }
    server.close(() => process.exit(0));
    setTimeout(() => process.exit(1), 2000).unref();
});
