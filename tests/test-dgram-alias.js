import assert from 'tjs:assert';

import dgramImport from 'dgram';
import nodeDgramImport from 'node:dgram';

const dgramRequireNs = require('dgram');
const nodeDgramRequireNs = require('node:dgram');
const dgramRequire = dgramRequireNs.default ?? dgramRequireNs;
const nodeDgramRequire = nodeDgramRequireNs.default ?? nodeDgramRequireNs;

for (const mod of [dgramImport, nodeDgramImport, dgramRequire, nodeDgramRequire]) {
    assert.eq(typeof mod.createSocket, 'function');
    assert.eq(typeof mod.Socket, 'function');
}

const socket = nodeDgramImport.createSocket('udp4');
assert.eq(typeof socket.bind, 'function');
assert.eq(typeof socket.send, 'function');
assert.eq(typeof socket.close, 'function');
assert.eq(typeof socket.connect, 'function');
assert.eq(typeof socket.address, 'function');
assert.eq(typeof socket.addMembership, 'function');
assert.eq(typeof socket.dropMembership, 'function');

socket.close();
