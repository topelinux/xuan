import assert from 'tjs:assert';

const legacy = require('string_decoder');
const modern = require('node:string_decoder');

assert.is(legacy.StringDecoder, modern.StringDecoder);

const { StringDecoder } = legacy;

const utf8 = new StringDecoder('utf8');
assert.equal(utf8.write(Buffer.from([ 0xe4 ])), '');
assert.equal(utf8.write(Buffer.from([ 0xbd, 0xa0 ])), '你');
assert.equal(utf8.end(), '');

const inherited = {};
StringDecoder.call(inherited, 'utf8');
assert.equal(inherited.encoding, 'utf8');
assert.equal(StringDecoder.prototype.write.call(inherited, Buffer.from('ok')), 'ok');
assert.equal(StringDecoder.prototype.end.call(inherited), '');

const utf16 = new StringDecoder('utf16le');
assert.equal(utf16.write(Buffer.from([ 0x41 ])), '');
assert.equal(utf16.end(Buffer.from([ 0x00, 0x42, 0x00 ])), 'AB');

const base64 = new StringDecoder('base64');
assert.equal(base64.write(Buffer.from([ 0x4d ])), '');
assert.equal(base64.end(Buffer.from([ 0x61 ])), 'TWE=');
