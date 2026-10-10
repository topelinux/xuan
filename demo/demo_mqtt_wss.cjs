const mqtt = require('mqtt');

const url = 'wss://test.mosquitto.org:8081/mqtt';

const client = mqtt.connect('wss://broker.hivemq.com:8884/mqtt', {
  clientId: 'test_' + Math.random().toString(16).slice(2),
  protocol: 'wss',
});
//const options = {
//  clientId: 'nodejs_wss_demo_' + Math.random().toString(16).slice(2, 8),
//  protocol: 'wss',
//  rejectUnauthorized: true,
//};

//const client = mqtt.connect(url, options);

client.on('connect', () => {
  console.log('✅ Connected via WSS');

  const topic = 'demo/wss/test';

  client.subscribe(topic, (err) => {
    if (!err) {
      console.log('📡 Subscribed:', topic);
      client.publish(topic, 'Hello over WSS');
    }
  });
});

client.on('message', (topic, message) => {
  console.log(`📩 ${topic}: ${message.toString()}`);
});

client.on('error', (err) => {
  console.error('❌ Error:', err);
});
