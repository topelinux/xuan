// Minimal "hello world" MQTT subscriber, used to measure the RSS of a small but
// realistic IoT application.
//
// Prints READY once subscribed, then stays connected.
//
// Env:
//   MQTT_URL    broker URL (default mqtt://127.0.0.1:1883)
//   MQTT_TOPIC  topic to subscribe to (default bench/hello)
const mqtt = require('mqtt');

const url = process.env.MQTT_URL || 'mqtt://127.0.0.1:1883';
const topic = process.env.MQTT_TOPIC || 'bench/hello';

const client = mqtt.connect(url);

client.on('error', (err) => {
    console.log('ERROR ' + err.message);
    process.exit(3);
});

client.on('connect', () => {
    client.subscribe(topic, { qos: 0 }, (err) => {
        if (err) {
            console.log('ERROR ' + err.message);
            process.exit(3);
        }
        console.log('READY');
    });
});
