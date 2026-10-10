const mqtt = require('mini-mqtt-client');
const fs = require('fs');

const caPem = fs.readFileSync('./broker.emqx.io-ca.crt');

const options = {
  protocol: 'mqtts',
  host: 'broker.emqx.io',
  port: 8883,
  servername: 'broker.emqx.io',
  rejectUnauthorized: true, // demo用，生产建议验证CA
  clientId: 'node_mqtts_demo_' + Math.random().toString(16).slice(2),
  ca: caPem
};

const client = mqtt.connect(options);

client.on('connect', () => {
  console.log('connected');

  client.subscribe('test/node/mqtts', (err) => {
    if (!err) {
      console.log('subscribed');

      client.publish('test/node/mqtts', 'hello mqtts');
    }
  });
});

client.on('message', (topic, message) => {
  console.log('recv:', topic, message.toString());
});

client.on('error', (err) => {
  console.error('error:', err);
});
