const mqtt = require('mini-mqtt-client');
const fs = require('fs');
const path = require('path');

const CA_PATH = path.join(__dirname, 'broker.emqx.io-ca.crt');
const caPem = fs.readFileSync(CA_PATH);

const RUNS = Number(process.env.RUNS || '0'); // 0 => infinite
const TIMEOUT_MS = Number(process.env.TIMEOUT_MS || '15000');
const SLEEP_MS = Number(process.env.SLEEP_MS || '1000');

const baseOptions = {
  protocol: 'mqtts',
  host: 'broker.emqx.io',
  port: 8883,
  servername: 'broker.emqx.io',
  rejectUnauthorized: true,
  ca: caPem
};

function once(client, event) {
  return new Promise((resolve) => client.once(event, resolve));
}

function delay(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

async function runOnce(index) {
  return new Promise((resolve) => {
    const options = {
      ...baseOptions,
      clientId: `node_mqtts_demo_${Math.random().toString(16).slice(2)}`
    };

    const client = mqtt.connect(options);
    let done = false;

    const finish = (label, err) => {
      if (done) return;
      done = true;
      if (err) {
        console.error(`[run ${index}] ${label}:`, err);
      } else {
        console.log(`[run ${index}] ${label}`);
      }
      client.end(true, () => resolve());
    };

    const timeout = setTimeout(() => {
      finish('timeout');
    }, TIMEOUT_MS);

    client.on('connect', () => {
      console.log(`[run ${index}] connected`);
      client.subscribe('test/node/mqtts', (err) => {
        if (err) {
          clearTimeout(timeout);
          finish('subscribe error', err);
          return;
        }
        console.log(`[run ${index}] subscribed`);
        client.publish('test/node/mqtts', 'hello mqtts');
      });
    });

    client.on('message', (topic, message) => {
      console.log(`[run ${index}] recv:`, topic, message.toString());
      clearTimeout(timeout);
      finish('ok');
    });

    client.on('error', (err) => {
      clearTimeout(timeout);
      finish('error', err);
    });

    client.on('close', () => {
      if (!done) {
        clearTimeout(timeout);
        finish('close');
      }
    });
  });
}
setInterval(() => {
  console.log('before gc')
  let mem = tjs.engine.gc.get();
  console.log(`[mem-leak] heapUsed=${mem.memoryUsedSize} bytes, objCount=${mem.objCount}`);

  tjs.engine.gc.run();
  console.log('before gc')
  mem = tjs.engine.gc.get();
  console.log(`[mem-leak] heapUsed=${mem.memoryUsedSize} bytes, objCount=${mem.objCount}`);
}, 5000);

(async () => {
  let i = 0;
  for (; ;) {
    i += 1;
    await runOnce(i);
    if (RUNS > 0 && i >= RUNS) break;
    await delay(SLEEP_MS);
  }
})();
