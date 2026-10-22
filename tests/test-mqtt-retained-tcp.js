// An in-process wire fixture, with no broker dependency.
import assert from 'tjs:assert';
import net from 'node:net';
import mqtt from 'tjs:mqtt';

// Packets delivered after SUBACK, in order: retained QoS0, live QoS0,
// retained QoS1. The QoS1 packet uses packet id 7 and expects a PUBACK.
const deliveries = [
    { payload: 'A', retain: true, qos: 0 },
    { payload: 'B', retain: false, qos: 0 },
    { payload: 'C', retain: true, qos: 1, packetId: 7 },
];

const peers = new Set();
let checkDone = null;
let pubackSeen = false;
let pubackId = -1;

const server = net.createServer(socket => {
    peers.add(socket);
    socket.on('close', () => peers.delete(socket));
    let pending = Buffer.alloc(0);
    socket.on('data', chunk => {
        pending = Buffer.concat([pending, chunk]);
        while (pending.length >= 2) {
            let remaining = 0, mult = 1, at = 1, byte;
            do {
                if (at >= pending.length) return;
                byte = pending[at++]; remaining += (byte & 127) * mult; mult *= 128;
            } while (byte & 128);
            if (pending.length < at + remaining) return;
            const type = pending[0] >> 4, body = pending.subarray(at, at + remaining);
            pending = pending.subarray(at + remaining);
            if (type === 1) socket.write(Buffer.from([0x20, 2, 0, 0]));
            if (type === 4) {
                // PUBACK for the QoS1 delivery, record the acknowledged id.
                if (body.length >= 2) {
                    pubackId = (body[0] << 8) | body[1];
                    pubackSeen = true;
                }
                checkDone?.();
            }
            if (type === 8) {
                // Grant the requested QoS (last byte of the SUBSCRIBE payload).
                socket.write(Buffer.from([0x90, 3, body[0], body[1], body[body.length - 1]]));
                // Remaining length: topic length (2) + "t" (1) + [packet id (2)] + payload (1).
                socket.write(Buffer.from([0x31, 4, 0, 1, 116, 65])); // RETAIN, QoS0, 'A'
                socket.write(Buffer.from([0x30, 4, 0, 1, 116, 66])); // QoS0, 'B'
                socket.write(Buffer.from([0x33, 6, 0, 1, 116, 0, 7, 67])); // RETAIN, QoS1, id 7, 'C'
            }
        }
    });
});

let client, timer, listening = false;
try {
    await new Promise((resolve, reject) => {
        server.once('error', error => {
            const message = String(error?.message || error);
            if (message.includes('EPERM') || message.includes('EACCES') || message.includes('operation not permitted')) {
                console.log('skip test-mqtt-retained-tcp:', message);
                resolve();
                return;
            }
            reject(error);
        });
        server.listen(0, '127.0.0.1', () => {
            listening = true;
            resolve();
        });
    });

    if (listening) {
        await new Promise((resolve, reject) => {
            let received = 0;

            const maybeDone = () => {
                if (received < deliveries.length || !pubackSeen) {
                    return;
                }
                try {
                    assert.eq(pubackId, deliveries[2].packetId);
                    resolve();
                } catch (error) { reject(error); }
            };
            checkDone = maybeDone;

            timer = setTimeout(() => reject(new Error('MQTT delivery fixture timeout')), 5000);
            client = mqtt.connect({host: '127.0.0.1', port: server.address().port, reconnectPeriod: 0});
            client.on('error', reject);
            client.on('connect', () => client.subscribe('t', {qos: 1}));
            client.on('message', (topic, payload, packet) => {
                try {
                    const expected = deliveries[received];
                    assert.ok(expected);
                    assert.eq(topic, 't');
                    assert.eq(payload.toString(), expected.payload);
                    assert.eq(packet.retain, expected.retain);
                    assert.eq(packet.qos, expected.qos);
                    received++;
                    maybeDone();
                } catch (error) { reject(error); }
            });
        });
        console.log('MQTT TCP retained, live and QoS1 delivery flags passed');
    }
} finally {
    checkDone = null;
    clearTimeout(timer);
    if (client) client.end(true);
    for (const socket of peers) socket.destroy();
    if (server.listening) {
        await new Promise(resolve => server.close(resolve));
    }
}
