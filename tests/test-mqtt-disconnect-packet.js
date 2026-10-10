const assert = require('tjs:assert');
const net = require('node:net');

const core = globalThis[Symbol.for('tjs.internal.core')];

await new Promise((resolve, reject) => {
  let client;
  let peer;
  let connected = false;
  let closed = false;
  let ticks = 0;

  const interval = setInterval(() => {
    ticks++;
  }, 10);

  const server = net.createServer((socket) => {
    peer = socket;

    socket.once('data', () => {
      socket.write(Buffer.from([0x20, 0x02, 0x00, 0x00])); // CONNACK accepted
      setTimeout(() => {
        socket.write(Buffer.from([0xe0, 0x00])); // DISCONNECT with no reason
      }, 20);
    });
  });

  const cleanup = (callback) => {
    clearInterval(interval);
    client?.close();
    peer?.destroy();
    if (server.listening) {
      server.close(callback);
    } else {
      callback();
    }
  };

  const finish = (error) => {
    if (closed) {
      return;
    }
    closed = true;
    cleanup(() => error ? reject(error) : resolve());
  };

  const timeout = setTimeout(() => {
    cleanup(() => reject(new Error('MQTT DISCONNECT packet did not close the client')));
  }, 2000);

  server.on('error', (error) => {
    clearTimeout(timeout);
    cleanup(() => {
      const message = String(error?.message || error);
      if (message.includes('EPERM') || message.includes('EACCES') || message.includes('operation not permitted')) {
        console.log('skip test-mqtt-disconnect-packet:', message);
        resolve();
        return;
      }
      reject(error);
    });
  });

  server.listen(0, '127.0.0.1', () => {
    const address = server.address();
    assert.ok(address);

    client = new core.MQTTClient({
      protocol: 'mqtt',
      host: '127.0.0.1',
      port: address.port,
      clientId: 'xuan-disconnect-regression',
      keepalive: 0,
    });

    client.onconnect = () => {
      connected = true;
    };

    client.onerror = () => {
      // The test asserts event loop progress and close delivery.
    };

    client.onclose = () => {
      clearTimeout(timeout);
      try {
        assert.eq(connected, true);
        assert.ok(ticks > 0);
      } catch (error) {
        finish(error);
        return;
      }
      finish();
    };
  });
});
