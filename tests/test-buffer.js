import assert from 'tjs:assert';
import legacy from 'buffer';
import modern from 'node:buffer';

assert.is(legacy, modern);
assert.is(legacy.Buffer, globalThis.Buffer);

const { Buffer } = legacy;

const hello = Buffer.from('hello');
assert.equal(hello.toString(), 'hello');

const header = Buffer.alloc(2);
header.writeUInt16BE(0x1234, 0);
assert.equal(header.readUInt16BE(0), 0x1234);

const joined = Buffer.concat([ Buffer.from('ab'), Buffer.from('cd') ]);
assert.equal(joined.toString(), 'abcd');

const source = new Uint8Array([ 65, 66, 67, 68 ]);
const shared = Buffer.from(source.buffer, 1, 2);
assert.equal(shared.toString(), 'BC');
source[1] = 90;
assert.equal(shared.toString(), 'ZC');

const copied = Buffer.from(shared);
source[2] = 89;
assert.equal(shared.toString(), 'ZY');
assert.equal(copied.toString(), 'ZC');

assert.equal(legacy.btoa('hi'), 'aGk=');
assert.equal(legacy.atob('aGk='), 'hi');

assert.equal(Buffer.from('OlhHMTIzNDU2Nzg=', 'base64').toString(), ':XG12345678');
assert.equal(Buffer.from('OlhHMTIzNDU2Nzg', 'base64').toString(), ':XG12345678');
assert.equal(Buffer.from('OlhHMTIzNDU2Nzg==', 'base64').toString(), ':XG12345678');
assert.equal(Buffer.from('====', 'base64').length, 0);
assert.equal(Buffer.from(' TWE=\n', 'base64').toString(), 'Ma');

const lineBuf = Buffer.from(new Uint8Array([ 13, 10, 79, 75, 13, 10 ]));
assert.equal(lineBuf.indexOf('\n'), 1);
assert.equal(lineBuf.indexOf('\n', 2), 5);
assert.equal(lineBuf.lastIndexOf('\n'), 5);
assert.equal(lineBuf.includes('\n'), true);
