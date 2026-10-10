import assert from 'tjs:assert';

const core = globalThis[Symbol.for('tjs.internal.core')];
const OriginalMQTTClient = core.MQTTClient;

class FakeMQTTClient {
    constructor(options) {
        this.options = options;
        FakeMQTTClient.instances.push(this);
    }
}

FakeMQTTClient.instances = [];
core.MQTTClient = FakeMQTTClient;

try {
    const mqttNs = require('mini-mqtt-client');
    const mqtt = mqttNs.default ?? mqttNs;
    const mqttNodeNs = require('node:mqtt');
    const mqttNode = mqttNodeNs.default ?? mqttNodeNs;

    assert.eq(typeof mqtt.connect, 'function');
    assert.eq(typeof mqtt.MqttClient, 'function');
    assert.eq(typeof mqttNode.connect, 'function');
    assert.eq(mqttNode.connect, mqtt.connect);
} finally {
    core.MQTTClient = OriginalMQTTClient;
}
