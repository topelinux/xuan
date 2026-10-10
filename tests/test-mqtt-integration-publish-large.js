import assert from 'tjs:assert';

const mqttNs = require('mini-mqtt-client');
const mqtt = mqttNs.default ?? mqttNs;

const host = tjs.env.MQTT_TEST_HOST;
const port = Number(tjs.env.MQTT_TEST_PORT || 1883);
const protocol = tjs.env.MQTT_TEST_PROTOCOL || 'mqtt';

if (!host) {
    console.log('skip test-mqtt-integration-publish-large: MQTT_TEST_HOST is not set');
} else {
    const topic = tjs.env.MQTT_TEST_TOPIC || `tjs/test/large/${Date.now()}/${Math.random().toString(16).slice(2, 8)}`;
    const payload = 'x'.repeat(Number(tjs.env.MQTT_TEST_LARGE_PAYLOAD_SIZE || 16384));
    const clientId = `tjs-large-${Math.random().toString(16).slice(2, 10)}`;

    const client = mqtt.connect({
        protocol,
        host,
        port,
        clientId,
        username: tjs.env.MQTT_TEST_USERNAME,
        password: tjs.env.MQTT_TEST_PASSWORD,
        keepalive: 10,
    });

    const timeout = setTimeout(() => {
        throw new Error('MQTT large publish integration test timeout');
    }, 20000);

    await new Promise((resolve, reject) => {
        let subscribed = false;

        client.once('connect', () => {
            client.subscribe(topic, { qos: 1 }, (err) => {
                if (err) {
                    reject(err);
                    return;
                }

                subscribed = true;
                client.publish(topic, payload, { qos: 1 }, (publishErr) => {
                    if (publishErr) {
                        reject(publishErr);
                    }
                });
            });
        });

        client.on('message', (recvTopic, message) => {
            if (!subscribed || recvTopic !== topic) {
                return;
            }

            try {
                assert.eq(message.length, payload.length);
                assert.eq(message.toString(), payload);
            } catch (err) {
                reject(err);
                return;
            }

            client.end(true, resolve);
        });

        client.on('error', reject);
        client.on('close', () => {
            clearTimeout(timeout);
        });
    });
}
