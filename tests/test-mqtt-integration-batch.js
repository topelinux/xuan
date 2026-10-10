import assert from 'tjs:assert';

const mqttNs = require('mini-mqtt-client');
const mqtt = mqttNs.default ?? mqttNs;

const host = tjs.env.MQTT_TEST_HOST;
const port = Number(tjs.env.MQTT_TEST_PORT || 1883);
const protocol = tjs.env.MQTT_TEST_PROTOCOL || 'mqtt';

if (!host) {
    console.log('skip test-mqtt-integration-batch: MQTT_TEST_HOST is not set');
} else {
    const prefix = tjs.env.MQTT_TEST_BATCH_PREFIX || `tjs/test/batch/${Date.now()}/${Math.random().toString(16).slice(2, 8)}`;
    const topics = Array.from({ length: 8 }, (_, index) => `${prefix}/${index}`);
    const publishTopic = topics[topics.length - 1];
    const payload = `hello-batch-${Date.now()}`;
    const clientId = `tjs-batch-${Math.random().toString(16).slice(2, 10)}`;

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
        throw new Error('MQTT batch integration test timeout');
    }, 20000);

    await new Promise((resolve, reject) => {
        let subscribed = false;

        client.once('connect', () => {
            client.subscribe(topics, { qos: 1 }, (err, granted) => {
                if (err) {
                    reject(err);
                    return;
                }

                try {
                    assert.eq(granted.length, topics.length);
                    assert.eq(granted[0].topic, topics[0]);
                    assert.eq(granted[topics.length - 1].topic, topics[topics.length - 1]);
                    assert.eq(granted[0].qos, 1);
                } catch (testErr) {
                    reject(testErr);
                    return;
                }

                subscribed = true;
                client.publish(publishTopic, payload, { qos: 1 }, (publishErr) => {
                    if (publishErr) {
                        reject(publishErr);
                    }
                });
            });
        });

        client.on('message', (recvTopic, message) => {
            if (!subscribed || recvTopic !== publishTopic) {
                return;
            }

            try {
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
