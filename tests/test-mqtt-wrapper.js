import assert from 'tjs:assert';

const core = globalThis[Symbol.for('tjs.internal.core')];
const OriginalMQTTClient = core.MQTTClient;
const originalSetTimeout = globalThis.setTimeout;
const originalClearTimeout = globalThis.clearTimeout;

function once(emitter, eventName) {
    return new Promise((resolve) => emitter.once(eventName, (...args) => resolve(args)));
}

class FakeMQTTClient {
    constructor(options) {
        this.options = options;
        this.calls = [];
        this.closed = false;
        FakeMQTTClient.instances.push(this);
    }

    publish(topic, payload, qos, retain, packetId) {
        this.calls.push({
            type: 'publish',
            topic,
            payload: Buffer.from(payload).toString(),
            qos,
            retain,
            packetId,
        });
    }

    subscribe(topics, packetId) {
        this.calls.push({ type: 'subscribe', topics, packetId });
    }

    unsubscribe(topics, packetId) {
        this.calls.push({ type: 'unsubscribe', topics, packetId });
    }

    close() {
        this.closed = true;
        this.calls.push({ type: 'close' });
    }
}

FakeMQTTClient.instances = [];
core.MQTTClient = FakeMQTTClient;

try {
    const mqttNs = require('mini-mqtt-client');
    const mqtt = mqttNs.default ?? mqttNs;

    {
        const client = mqtt.connect({
            protocol: 'mqtt',
            host: 'broker.local',
            port: 1883,
            auth: 'user:pass',
            will: {
                topic: 'will/topic',
                payload: 'offline',
                qos: 1,
                retain: true,
            },
        });
        const raw = FakeMQTTClient.instances.at(-1);

        assert.eq(raw.options.username, 'user');
        assert.eq(raw.options.password, 'pass');
        assert.eq(typeof raw.options.clientId, 'string');
        assert.ok(raw.options.clientId.length > 0);
        assert.eq(raw.options.auth, 'user:pass');
        assert.eq(raw.options.will.topic, 'will/topic');
        assert.eq(raw.options.will.payload, 'offline');
        assert.eq(raw.options.will.qos, 1);
        assert.eq(raw.options.will.retain, true);

        let publishCalled = false;
        client.publish('devices/1', 'queued', () => {
            publishCalled = true;
        });
        assert.eq(raw.calls.length, 0);

        const connectPromise = once(client, 'connect');
        raw.onconnect({ returnCode: 0, sessionPresent: false });
        await connectPromise;
        assert.eq(raw.calls[0].type, 'publish');
        assert.eq(raw.calls[0].payload, 'queued');
        assert.eq(publishCalled, true);

        let subscribeResult;
        client.subscribe({ 'topic/a': 1, 'topic/b': 0 }, (err, granted) => {
            assert.eq(err, null);
            subscribeResult = granted;
        });
        assert.eq(raw.calls[1].type, 'subscribe');
        assert.eq(raw.calls[1].topics.length, 2);
        raw.onsubscribed();
        assert.eq(subscribeResult.length, 2);
        assert.eq(subscribeResult[0].topic, 'topic/a');
        assert.eq(subscribeResult[0].qos, 1);

        let subscribeError;
        client.subscribe('topic/rejected', (err) => {
            subscribeError = err;
        });
        assert.eq(raw.calls[2].type, 'subscribe');
        raw.onsubscribed(new Error('MQTT subscribe failed'));
        assert.eq(subscribeError.message, 'MQTT subscribe failed');

        let received;
        const messagePromise = once(client, 'message');
        client.on('message', (topic, payload, packet) => {
            received = { topic, payload: payload.toString(), packet };
        });
        raw.onmessage('topic/a', new Uint8Array([104, 105]).buffer, 1, false);
        await messagePromise;
        assert.eq(received.topic, 'topic/a');
        assert.eq(received.payload, 'hi');
        assert.eq(received.packet.qos, 1);

        let unsubscribed = false;
        client.unsubscribe(['topic/a', 'topic/b'], (err) => {
            assert.eq(err, null);
            unsubscribed = true;
        });
        raw.onunsubscribed();
        assert.eq(unsubscribed, true);

        let ended = false;
        client.publish('devices/1', 'pending-close', { qos: 1 }, (err) => {
            assert.eq(err, null);
        });
        client.end(false, () => {
            ended = true;
        });
        assert.eq(raw.closed, false);
        raw.onack();
        assert.eq(raw.closed, true);
        raw.onclose(true);
        assert.eq(ended, true);
    }

    {
        const client = mqtt.connect({
            protocol: 'mqtt',
            host: 'broker.local',
            port: 1883,
        });
        const raw = FakeMQTTClient.instances.at(-1);
        const connectPromise = once(client, 'connect');

        raw.onconnect({ returnCode: 0, sessionPresent: false });
        await connectPromise;

        let callbackCount = 0;
        client.publish('devices/2', 'no-ack-needed', { qos: 0 }, (err) => {
            assert.eq(err, null);
            callbackCount++;
        });

        assert.eq(raw.calls.at(-1).type, 'publish');
        assert.eq(callbackCount, 1);

        let ended = false;
        client.end(false, () => {
            ended = true;
        });

        assert.eq(raw.closed, true);
        raw.onclose(true);
        assert.eq(ended, true);
    }

    {
        const client = mqtt.connect('mqtts://emqx:public@broker.emqx.io:8883', {
            clientId: 'url-connect-demo',
            rejectUnauthorized: false,
        });
        const raw = FakeMQTTClient.instances.at(-1);

        assert.eq(raw.options.protocol, 'mqtts');
        assert.eq(raw.options.host, 'broker.emqx.io');
        assert.eq(raw.options.port, 8883);
        assert.eq(raw.options.username, 'emqx');
        assert.eq(raw.options.password, 'public');
        assert.eq(raw.options.clientId, 'url-connect-demo');
        assert.eq(raw.options.rejectUnauthorized, false);

        client.end(true);
    }

    {
        const client = mqtt.connect('wss://emqx:public@broker.hivemq.com:8884/mqtt?foo=1', {
            clientId: 'wss-url-demo',
            rejectUnauthorized: false,
        });
        const raw = FakeMQTTClient.instances.at(-1);

        assert.eq(raw.options.protocol, 'wss');
        assert.eq(raw.options.host, 'broker.hivemq.com');
        assert.eq(raw.options.port, 8884);
        assert.eq(raw.options.path, '/mqtt?foo=1');
        assert.eq(raw.options.username, 'emqx');
        assert.eq(raw.options.password, 'public');
        assert.eq(raw.options.clientId, 'wss-url-demo');
        assert.eq(raw.options.rejectUnauthorized, false);

        client.end(true);
    }

    {
        const client = mqtt.connect('mqtt+wss://broker.hivemq.com/mqtt', {
            clientId: 'mqtt-plus-wss-demo',
        });
        const raw = FakeMQTTClient.instances.at(-1);

        assert.eq(raw.options.protocol, 'wss');
        assert.eq(raw.options.host, 'broker.hivemq.com');
        assert.eq(raw.options.port, 443);
        assert.eq(raw.options.path, '/mqtt');
        assert.eq(raw.options.clientId, 'mqtt-plus-wss-demo');

        client.end(true);
    }

    {
        const client = mqtt.connect({
            protocol: 'ws',
            host: 'broker.local',
        });
        const raw = FakeMQTTClient.instances.at(-1);

        assert.eq(raw.options.protocol, 'ws');
        assert.eq(raw.options.port, 80);
        assert.eq(raw.options.path, '/mqtt');

        client.end(true);
    }

    {
        let reconnectDelay;
        globalThis.setTimeout = (fn, delay) => {
            reconnectDelay = delay;
            fn();
            return 1;
        };
        globalThis.clearTimeout = () => { };

        const client = mqtt.connect({
            protocol: 'mqtt',
            host: 'broker.local',
            port: 1883,
        });
        const firstRaw = FakeMQTTClient.instances.at(-1);
        const events = [];
        let closeBeforeConnectError;

        client.on('offline', () => events.push('offline'));
        client.on('reconnect', () => events.push('reconnect'));
        client.on('error', (err) => {
            closeBeforeConnectError = err;
        });

        firstRaw.onclose(false);

        assert.eq(closeBeforeConnectError.message, 'MQTT connection closed before connect');
        assert.eq(events[0], 'offline');
        assert.eq(events[1], 'reconnect');
        assert.eq(reconnectDelay, 1000);
        assert.eq(FakeMQTTClient.instances.length >= 2, true);
    }

    {
        let reconnectDelay;
        globalThis.setTimeout = (fn, delay) => {
            reconnectDelay = delay;
            fn();
            return 1;
        };
        globalThis.clearTimeout = () => { };

        const client = mqtt.connect({
            protocol: 'mqtt',
            host: 'broker.local',
            port: 1883,
            reconnectPeriod: 250,
        });
        const firstRaw = FakeMQTTClient.instances.at(-1);
        const events = [];

        client.on('offline', () => events.push('offline'));
        client.on('reconnect', () => events.push('reconnect'));
        client.on('error', () => { });

        firstRaw.onclose(false);

        assert.eq(events[0], 'offline');
        assert.eq(events[1], 'reconnect');
        assert.eq(reconnectDelay, 250);
    }

    {
        let timeoutCalled = false;
        globalThis.setTimeout = () => {
            timeoutCalled = true;
            return 1;
        };
        globalThis.clearTimeout = () => { };

        const client = mqtt.connect({
            protocol: 'mqtt',
            host: 'broker.local',
            port: 1883,
            reconnectPeriod: 0,
        });
        const raw = FakeMQTTClient.instances.at(-1);
        const events = [];

        client.on('offline', () => events.push('offline'));
        client.on('reconnect', () => events.push('reconnect'));
        client.on('error', () => { });

        raw.onclose(false);

        assert.eq(timeoutCalled, false);
        assert.eq(events.length, 0);
        assert.eq(client.debugInfo.reconnecting, false);
    }

    {
        const client = mqtt.connect({
            protocol: 'mqtt',
            servers: [
                { host: 'broker-1.local', port: 1883 },
                { host: 'broker-2.local', port: 1884 },
            ],
        });
        const firstRaw = FakeMQTTClient.instances.at(-1);
        assert.eq(firstRaw.options.host, 'broker-1.local');
        assert.eq(firstRaw.options.port, 1883);

        globalThis.setTimeout = (fn) => {
            fn();
            return 1;
        };
        globalThis.clearTimeout = () => { };
        client.on('error', () => { });

        firstRaw.onclose(false);

        const secondRaw = FakeMQTTClient.instances.at(-1);
        assert.eq(secondRaw.options.host, 'broker-2.local');
        assert.eq(secondRaw.options.port, 1884);
    }

    {
        const client = mqtt.connect({
            protocol: 'mqtts',
            rejectUnauthorized: false,
            host: 'broker.local',
            port: 8883,
        });
        const raw = FakeMQTTClient.instances.at(-1);
        assert.eq(raw.options.rejectUnauthorized, false);
        client.end(true);
    }

    {
        const client = mqtt.connect({
            protocol: 'mqtt',
            host: 'broker.local',
            port: 1883,
        });
        const raw = FakeMQTTClient.instances.at(-1);

        client.end(true);
        assert.eq(raw.closed, true);
    }

    {
        class ThrowingMQTTClient {
            constructor() {
                const err = new Error('MQTT CA parse failed: 1 certificate(s) failed to parse');
                err.code = 1;
                throw err;
            }
        }

        core.MQTTClient = ThrowingMQTTClient;

        globalThis.setTimeout = () => 1;
        globalThis.clearTimeout = () => { };

        const client = mqtt.connect({
            protocol: 'mqtts',
            host: 'broken.local',
            port: 8883,
        });

        let errorEvent;
        let closeEventCount = 0;
        client.on('error', (err) => {
            errorEvent = err;
        });
        client.on('close', () => {
            closeEventCount++;
        });

        await Promise.resolve();

        assert.eq(client.connected, false);
        assert.eq(errorEvent instanceof Error, true);
        assert.eq(errorEvent.message, 'MQTT CA parse failed: 1 certificate(s) failed to parse');
        assert.eq(errorEvent.code, 1);
        assert.eq(closeEventCount, 1);

        core.MQTTClient = FakeMQTTClient;
    }
} finally {
    core.MQTTClient = OriginalMQTTClient;
    globalThis.setTimeout = originalSetTimeout;
    globalThis.clearTimeout = originalClearTimeout;
}
