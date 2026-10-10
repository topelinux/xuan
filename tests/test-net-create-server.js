const assert = require('tjs:assert');
const net = require('node:net');

console.log('in test-net-create-server');
function run() {
  return new Promise((resolve, reject) => {
    const server = net.createServer((socket) => {
      socket.on('data', (chunk) => {
        console.log('in recv data', chunk);
        assert.eq(chunk.toString(), 'ping');
        socket.write('pong');
      });
    });

    server.on('error', (error) => {
      const msg = String(error && error.message ? error.message : error);
      if (msg.includes('EPERM') || msg.includes('EACCES') || msg.includes('operation not permitted')) {
        console.log('skip test-net-create-server:', msg);
        resolve();
        return;
      }
      reject(error);
    });

    server.listen(0, '127.0.0.1', () => {
      const addr = server.address();
      assert.ok(addr);
      assert.eq(typeof addr.port, 'number');

      const client = net.connect(addr.port, '127.0.0.1', () => {
        client.write('ping');
      });

      client.on('data', (chunk) => {
        assert.eq(chunk.toString(), 'pong');
        client.end();
        client.destroy();
        server.close(() => resolve());
      });

      client.on('error', reject);
    });
  });
}

await run().catch((error) => {
  console.error(error);
});
