import assert from 'tjs:assert';

const mqttNs = require('mini-mqtt-client');
const mqtt = mqttNs.default ?? mqttNs;

const host = tjs.env.MQTT_TEST_HOST;
const port = Number(tjs.env.MQTT_TEST_PORT || 1883);
const protocol = tjs.env.MQTT_TEST_PROTOCOL || 'mqtt';

if (!host) {
    console.log('skip test-mqtt-integration: MQTT_TEST_HOST is not set');
} else {
    const topic = tjs.env.MQTT_TEST_TOPIC || `tjs/test/${Date.now()}/${Math.random().toString(16).slice(2, 8)}`;
    const payload = `hello-${Date.now()}`;
    const clientId = `tjs-it-${Math.random().toString(16).slice(2, 10)}`;

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
        throw new Error('MQTT integration test timeout');
    }, 15000);

    await new Promise((resolve, reject) => {
        let subscribed = false;

        client.once('connect', () => {
            client.subscribe(topic, { qos: 0 }, (err) => {
                if (err) {
                    reject(err);
                    return;
                }

                subscribed = true;
                client.publish(topic, payload, { qos: 0 }, (publishErr) => {
                    if (publishErr) {
                        reject(publishErr);
                    }
                });
            });
        });

        client.on('message', (recvTopic, message) => {
            if (!subscribed) {
                return;
            }

            try {
                assert.eq(recvTopic, topic);
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
