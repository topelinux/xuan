// Buffer compatibility test: xuan vs Node.js v24.14.1
// Run with: node test_buffer_compat.js (Node.js)
// Run with: ./build/xuan run test_buffer_compat.js (xuan)
// Compare outputs to find differences

const { Buffer } = globalThis;
const results = [];
let passed = 0;
let failed = 0;

function test(name, fn) {
    try {
        const result = fn();
        results.push({ name, status: 'PASS', result: String(result) });
        passed++;
    } catch (e) {
        results.push({ name, status: 'FAIL', error: e.message });
        failed++;
    }
}

function assertEqual(actual, expected, msg = '') {
    if (actual !== expected) {
        throw new Error(`${msg}\n  expected: ${expected}\n  actual:   ${actual}`);
    }
}

// ===== Buffer constructor / globalThis.Buffer =====

test('Buffer exists on globalThis', () => {
    if (typeof globalThis.Buffer !== 'function') throw new Error('Buffer not found');
});

test('Buffer extends Uint8Array', () => {
    const b = Buffer.alloc(1);
    if (!(b instanceof Uint8Array)) throw new Error('Buffer should extend Uint8Array');
});

test('Buffer.prototype.isBuffer', () => {
    const b = Buffer.alloc(1);
    // Node.js v24 does not expose the deprecated instance method.
    assertEqual(typeof b.isBuffer, 'undefined');
});

// ===== Buffer.isBuffer =====

test('Buffer.isBuffer - true for Buffer', () => {
    if (!Buffer.isBuffer(Buffer.alloc(1))) throw new Error('should be true');
});

test('Buffer.isBuffer - false for Uint8Array', () => {
    if (Buffer.isBuffer(new Uint8Array(1))) throw new Error('should be false');
});

test('Buffer.isBuffer - false for plain object', () => {
    if (Buffer.isBuffer({})) throw new Error('should be false');
});

// ===== Buffer.isEncoding =====

test('Buffer.isEncoding - utf8', () => assertEqual(Buffer.isEncoding('utf8'), true));
test('Buffer.isEncoding - utf-8', () => assertEqual(Buffer.isEncoding('utf-8'), true));
test('Buffer.isEncoding - hex', () => assertEqual(Buffer.isEncoding('hex'), true));
test('Buffer.isEncoding - base64', () => assertEqual(Buffer.isEncoding('base64'), true));
test('Buffer.isEncoding - latin1', () => assertEqual(Buffer.isEncoding('latin1'), true));
test('Buffer.isEncoding - binary', () => assertEqual(Buffer.isEncoding('binary'), true));
test('Buffer.isEncoding - ascii', () => assertEqual(Buffer.isEncoding('ascii'), true));
test('Buffer.isEncoding - ucs2', () => assertEqual(Buffer.isEncoding('ucs2'), true));
test('Buffer.isEncoding - ucs-2', () => assertEqual(Buffer.isEncoding('ucs-2'), true));
test('Buffer.isEncoding - utf16le', () => assertEqual(Buffer.isEncoding('utf16le'), true));
test('Buffer.isEncoding - invalid', () => assertEqual(Buffer.isEncoding('invalid'), false));

// ===== Buffer.alloc =====

test('Buffer.alloc - zero-fill', () => {
    const b = Buffer.alloc(5);
    for (let i = 0; i < 5; i++) {
        if (b[i] !== 0) throw new Error(`byte ${i} should be 0, got ${b[i]}`);
    }
});

test('Buffer.alloc - with fill number', () => {
    const b = Buffer.alloc(5, 0xFF);
    for (let i = 0; i < 5; i++) {
        if (b[i] !== 0xFF) throw new Error(`byte ${i} should be 255, got ${b[i]}`);
    }
});

test('Buffer.alloc - with fill string', () => {
    const b = Buffer.alloc(6, 'abc');
    assertEqual(b.toString(), 'abcabc');
});

test('Buffer.alloc - with fill string and encoding', () => {
    const b = Buffer.alloc(4, 'ff', 'hex');
    assertEqual(b[0], 0xFF);
    assertEqual(b[1], 0xFF);
    assertEqual(b[2], 0xFF);
    assertEqual(b[3], 0xFF);
});

test('Buffer.alloc - with fill Buffer', () => {
    const b = Buffer.alloc(6, Buffer.from('ab'));
    assertEqual(b.toString(), 'ababab');
});

test('Buffer.alloc - size 0', () => {
    const b = Buffer.alloc(0);
    assertEqual(b.length, 0);
});

test('Buffer.alloc - negative size throws', () => {
    let threw = false;
    try { Buffer.alloc(-1); } catch { threw = true; }
    if (!threw) throw new Error('should throw');
});

// ===== Buffer.allocUnsafe =====

test('Buffer.allocUnsafe - creates buffer', () => {
    const b = Buffer.allocUnsafe(5);
    assertEqual(b.length, 5);
});

// ===== Buffer.allocUnsafeSlow =====

test('Buffer.allocUnsafeSlow - exists', () => {
    if (typeof Buffer.allocUnsafeSlow !== 'function') throw new Error('allocUnsafeSlow missing');
    const b = Buffer.allocUnsafeSlow(5);
    assertEqual(b.length, 5);
});

// ===== Buffer.from =====

test('Buffer.from - string utf8', () => {
    const b = Buffer.from('hello');
    assertEqual(b.toString(), 'hello');
});

test('Buffer.from - string with encoding', () => {
    const b = Buffer.from('68656c6c6f', 'hex');
    assertEqual(b.toString(), 'hello');
});

test('Buffer.from - base64', () => {
    const b = Buffer.from('aGVsbG8=', 'base64');
    assertEqual(b.toString(), 'hello');
});

test('Buffer.from - base64url', () => {
    const b = Buffer.from('aGVsbG8', 'base64url');
    assertEqual(b.toString(), 'hello');
});

test('Buffer.from - latin1', () => {
    const b = Buffer.from('Ééñü', 'latin1');
    assertEqual(b.length, 4);
    assertEqual(b[0], 0xC9);
    assertEqual(b[1], 0xE9);
    assertEqual(b[2], 0xF1);
    assertEqual(b[3], 0xFC);
});

test('Buffer.from - ascii', () => {
    const b = Buffer.from('hello', 'ascii');
    assertEqual(b.toString('ascii'), 'hello');
});

test('Buffer.from - array', () => {
    const b = Buffer.from([1, 2, 3]);
    assertEqual(b[0], 1);
    assertEqual(b[1], 2);
    assertEqual(b[2], 3);
});

test('Buffer.from - array with offset/length', () => {
    const b = Buffer.from([1, 2, 3, 4, 5], 1, 3);
    // Node.js ignores offset/length for array input.
    assertEqual(b.length, 5);
    assertEqual(b[0], 1);
    assertEqual(b[4], 5);
});

test('Buffer.from - ArrayBuffer', () => {
    const ab = new ArrayBuffer(4);
    new Uint8Array(ab).set([1, 2, 3, 4]);
    const b = Buffer.from(ab);
    assertEqual(b.length, 4);
    assertEqual(b[0], 1);
});

test('Buffer.from - TypedArray', () => {
    const ta = new Uint8Array([1, 2, 3]);
    const b = Buffer.from(ta);
    assertEqual(b.length, 3);
    assertEqual(b[0], 1);
});

test('Buffer.from - TypedArray copies', () => {
    const ta = new Uint8Array([1, 2, 3]);
    const b = Buffer.from(ta);
    ta[0] = 99;
    assertEqual(b[0], 1);
});

test('Buffer.from - ArrayBuffer shares memory', () => {
    const ab = new ArrayBuffer(3);
    const view = new Uint8Array(ab);
    view.set([1, 2, 3]);
    const b = Buffer.from(ab);
    view[0] = 99;
    assertEqual(b[0], 99);
});

test('Buffer.from - Buffer from Buffer copies', () => {
    const b1 = Buffer.from([1, 2, 3]);
    const b2 = Buffer.from(b1);
    b1[0] = 99;
    assertEqual(b2[0], 1);
});

// ===== Buffer.byteLength =====

test('Buffer.byteLength - string', () => {
    assertEqual(Buffer.byteLength('hello'), 5);
});

test('Buffer.byteLength - utf8 string', () => {
    assertEqual(Buffer.byteLength('你好'), 6);
});

test('Buffer.byteLength - base64 string', () => {
    assertEqual(Buffer.byteLength('aGVsbG8=', 'base64'), 5);
});

test('Buffer.byteLength - hex string', () => {
    assertEqual(Buffer.byteLength('68656c6c6f', 'hex'), 5);
});

test('Buffer.byteLength - Buffer', () => {
    assertEqual(Buffer.byteLength(Buffer.from('hello')), 5);
});

test('Buffer.byteLength - ArrayBuffer', () => {
    assertEqual(Buffer.byteLength(new ArrayBuffer(5)), 5);
});

// ===== Buffer.concat =====

test('Buffer.concat - basic', () => {
    const b = Buffer.concat([Buffer.from('a'), Buffer.from('b')]);
    assertEqual(b.toString(), 'ab');
});

test('Buffer.concat - with totalLength', () => {
    const b = Buffer.concat([Buffer.from('abc'), Buffer.from('de')], 4);
    assertEqual(b.toString(), 'abcd');
});

test('Buffer.concat - single item', () => {
    const src = Buffer.from('abc');
    const b = Buffer.concat([src]);
    assertEqual(b.toString(), 'abc');
});

test('Buffer.concat - empty array', () => {
    const b = Buffer.concat([]);
    assertEqual(b.length, 0);
});

// ===== Buffer.constants =====

test('Buffer.constants.MAX_LENGTH', () => {
    const constants = typeof require !== 'undefined' ? require('buffer').constants : Buffer.constants;
    if (!constants) return;
    if (typeof constants.MAX_LENGTH !== 'number') throw new Error('missing');
});

test('Buffer.constants.MAX_STRING_LENGTH', () => {
    const constants = typeof require !== 'undefined' ? require('buffer').constants : Buffer.constants;
    if (!constants) return;
    if (typeof constants.MAX_STRING_LENGTH !== 'number') throw new Error('missing');
});

// ===== buffer.toString() =====

test('toString - default utf8', () => {
    assertEqual(Buffer.from('hello').toString(), 'hello');
});

test('toString - hex', () => {
    assertEqual(Buffer.from([0xFF, 0x00]).toString('hex'), 'ff00');
});

test('toString - base64', () => {
    assertEqual(Buffer.from('hello').toString('base64'), 'aGVsbG8=');
});

test('toString - base64url', () => {
    assertEqual(Buffer.from([ 0x1d, 0x18, 0x4d, 0x8e, 0x4d, 0xe8, 0xe5, 0xa6 ]).toString('base64url'), 'HRhNjk3o5aY');
});

test('toString - latin1', () => {
    const b = Buffer.from([0xC9, 0xE9, 0xF1, 0xFC]);
    const s = b.toString('latin1');
    assertEqual(s.length, 4);
    assertEqual(s.charCodeAt(0), 0xC9);
});

test('toString - ascii', () => {
    assertEqual(Buffer.from('hello').toString('ascii'), 'hello');
});

test('toString - utf16le', () => {
    // Node.js supports utf16le
    // "A" = 0x0041 in UTF-16LE = [65, 0]
    const b = Buffer.from([65, 0, 66, 0]);
    const s = b.toString('utf16le');
    assertEqual(s, 'AB');
});

test('toString - ucs2', () => {
    const b = Buffer.from([65, 0, 66, 0]);
    const s = b.toString('ucs2');
    assertEqual(s, 'AB');
});

test('toString - with start/end', () => {
    assertEqual(Buffer.from('hello').toString('utf8', 1, 4), 'ell');
});

// ===== buffer.write() =====

test('write - basic', () => {
    const b = Buffer.alloc(10);
    const written = b.write('hello');
    assertEqual(written, 5);
    assertEqual(b.toString('utf8', 0, 5), 'hello');
});

test('write - with offset', () => {
    const b = Buffer.alloc(10);
    b.write('hello', 2);
    assertEqual(b.toString('utf8', 2, 7), 'hello');
});

test('write - with length limit', () => {
    const b = Buffer.alloc(10);
    b.write('hello world', 0, 5);
    assertEqual(b.toString('utf8', 0, 5), 'hello');
});

test('write - with encoding', () => {
    const b = Buffer.alloc(4);
    b.write('ff00ff00', 0, 'hex');
    assertEqual(b[0], 0xFF);
    assertEqual(b[1], 0x00);
});

// ===== buffer.copy() =====

test('copy - basic', () => {
    const src = Buffer.from('abcd');
    const target = Buffer.alloc(4);
    const copied = src.copy(target);
    assertEqual(copied, 4);
    assertEqual(target.toString(), 'abcd');
});

test('copy - with offsets', () => {
    const src = Buffer.from('abcd');
    const target = Buffer.alloc(6);
    src.copy(target, 1, 1, 3);
    assertEqual(target.toString('utf8', 1, 3), 'bc');
});

// ===== buffer.subarray() / buffer.slice() =====

test('subarray - basic', () => {
    const b = Buffer.from([1, 2, 3, 4, 5]);
    const sub = b.subarray(1, 4);
    assertEqual(sub.length, 3);
    assertEqual(sub[0], 2);
});

test('subarray - shares memory', () => {
    const b = Buffer.from([1, 2, 3]);
    const sub = b.subarray(1);
    sub[0] = 99;
    assertEqual(b[1], 99);
});

test('subarray - negative indices', () => {
    const b = Buffer.from([1, 2, 3, 4, 5]);
    const sub = b.subarray(-3, -1);
    assertEqual(sub[0], 3);
    assertEqual(sub[1], 4);
});

// ===== indexOf / lastIndexOf / includes =====

test('indexOf - string needle', () => {
    assertEqual(Buffer.from('hello world').indexOf('world'), 6);
});

test('indexOf - buffer needle', () => {
    assertEqual(Buffer.from('hello world').indexOf(Buffer.from('world')), 6);
});

test('indexOf - number needle', () => {
    assertEqual(Buffer.from('hello world').indexOf(111), 4); // 'o' = 111
});

test('indexOf - not found', () => {
    assertEqual(Buffer.from('hello').indexOf('xyz'), -1);
});

test('indexOf - with offset', () => {
    assertEqual(Buffer.from('hello hello').indexOf('hello', 1), 6);
});

test('indexOf - encoding param', () => {
    assertEqual(Buffer.from([0x68, 0x65, 0x6c, 0x6c, 0x6f]).indexOf('6865', 0, 'hex'), 0);
});

test('lastIndexOf - basic', () => {
    assertEqual(Buffer.from('hello hello').lastIndexOf('hello'), 6);
});

test('lastIndexOf - with offset', () => {
    assertEqual(Buffer.from('hello hello').lastIndexOf('hello', 5), 0);
});

test('includes - string', () => {
    assertEqual(Buffer.from('hello').includes('ell'), true);
});

test('includes - buffer', () => {
    assertEqual(Buffer.from('hello').includes(Buffer.from('ell')), true);
});

test('includes - number', () => {
    assertEqual(Buffer.from('hello').includes(108), true); // 'l' = 108
});

// ===== Read methods =====

test('readUInt8', () => {
    const b = Buffer.from([0xFF]);
    assertEqual(b.readUInt8(0), 255);
});

test('readInt8', () => {
    const b = Buffer.from([0xFF]);
    assertEqual(b.readInt8(0), -1);
});

test('readUInt16LE', () => {
    const b = Buffer.from([0x34, 0x12]);
    assertEqual(b.readUInt16LE(0), 0x1234);
});

test('readUInt16BE', () => {
    const b = Buffer.from([0x12, 0x34]);
    assertEqual(b.readUInt16BE(0), 0x1234);
});

test('readInt16LE', () => {
    const b = Buffer.from([0xFF, 0xFF]);
    assertEqual(b.readInt16LE(0), -1);
});

test('readUInt32LE', () => {
    const b = Buffer.from([0x78, 0x56, 0x34, 0x12]);
    assertEqual(b.readUInt32LE(0), 0x12345678);
});

test('readUInt32BE', () => {
    const b = Buffer.from([0x12, 0x34, 0x56, 0x78]);
    assertEqual(b.readUInt32BE(0), 0x12345678);
});

test('readFloatLE', () => {
    const b = Buffer.alloc(4);
    b.writeFloatLE(3.14159, 0);
    const val = b.readFloatLE(0);
    if (Math.abs(val - 3.14159) > 0.0001) throw new Error(`got ${val}`);
});

test('readDoubleLE', () => {
    const b = Buffer.alloc(8);
    b.writeDoubleLE(Math.PI, 0);
    const val = b.readDoubleLE(0);
    if (Math.abs(val - Math.PI) > 1e-10) throw new Error(`got ${val}`);
});

test('readBigInt64LE', () => {
    const b = Buffer.alloc(8);
    b.writeBigInt64LE(-12345678901234n, 0);
    const val = b.readBigInt64LE(0);
    assertEqual(val, -12345678901234n);
});

test('readBigUInt64LE', () => {
    const b = Buffer.alloc(8);
    b.writeBigUInt64LE(12345678901234n, 0);
    const val = b.readBigUInt64LE(0);
    assertEqual(val, 12345678901234n);
});

// Aliases
test('readUint8 (alias)', () => {
    const b = Buffer.from([0xFF]);
    assertEqual(b.readUint8(0), 255);
});

test('readUint16LE (alias)', () => {
    const b = Buffer.from([0x34, 0x12]);
    assertEqual(b.readUint16LE(0), 0x1234);
});

test('readUint32LE (alias)', () => {
    const b = Buffer.from([0x78, 0x56, 0x34, 0x12]);
    assertEqual(b.readUint32LE(0), 0x12345678);
});

test('readBigUint64LE (alias)', () => {
    const b = Buffer.alloc(8);
    b.writeBigUInt64LE(12345678901234n, 0);
    const val = b.readBigUint64LE(0);
    assertEqual(val, 12345678901234n);
});

// ===== Write methods =====

test('writeUInt8', () => {
    const b = Buffer.alloc(1);
    b.writeUInt8(0xFF, 0);
    assertEqual(b[0], 0xFF);
});

test('writeInt8', () => {
    const b = Buffer.alloc(1);
    b.writeInt8(-1, 0);
    assertEqual(b[0], 0xFF);
});

test('writeUInt16LE', () => {
    const b = Buffer.alloc(2);
    b.writeUInt16LE(0x1234, 0);
    assertEqual(b[0], 0x34);
    assertEqual(b[1], 0x12);
});

test('writeUInt16BE', () => {
    const b = Buffer.alloc(2);
    b.writeUInt16BE(0x1234, 0);
    assertEqual(b[0], 0x12);
    assertEqual(b[1], 0x34);
});

test('writeUInt32LE', () => {
    const b = Buffer.alloc(4);
    b.writeUInt32LE(0x12345678, 0);
    assertEqual(b[0], 0x78);
    assertEqual(b[1], 0x56);
    assertEqual(b[2], 0x34);
    assertEqual(b[3], 0x12);
});

test('writeFloatLE', () => {
    const b = Buffer.alloc(4);
    b.writeFloatLE(3.14159, 0);
    const val = b.readFloatLE(0);
    if (Math.abs(val - 3.14159) > 0.0001) throw new Error(`got ${val}`);
});

test('writeDoubleLE', () => {
    const b = Buffer.alloc(8);
    b.writeDoubleLE(Math.PI, 0);
    const val = b.readDoubleLE(0);
    if (Math.abs(val - Math.PI) > 1e-10) throw new Error(`got ${val}`);
});

test('writeBigInt64LE', () => {
    const b = Buffer.alloc(8);
    b.writeBigInt64LE(-12345678901234n, 0);
    const val = b.readBigInt64LE(0);
    assertEqual(val, -12345678901234n);
});

test('writeBigUInt64LE', () => {
    const b = Buffer.alloc(8);
    b.writeBigUInt64LE(12345678901234n, 0);
    const val = b.readBigUInt64LE(0);
    assertEqual(val, 12345678901234n);
});

// ===== Slice methods (internal methods used by busboy) =====

test('latin1Slice - basic', () => {
    const b = Buffer.from('Hello');
    assertEqual(b.latin1Slice(0, 5), 'Hello');
});

test('latin1Slice - extended bytes', () => {
    const b = Buffer.from([0xC9, 0xE9, 0xF1, 0xFC]);
    const s = b.latin1Slice(0, 4);
    assertEqual(s.length, 4);
    assertEqual(s.charCodeAt(0), 0xC9);
});

test('utf8Slice - basic', () => {
    const b = Buffer.from('你好');
    assertEqual(b.utf8Slice(0, b.length), '你好');
});

test('ucs2Slice - basic', () => {
    const b = Buffer.from([65, 0, 66, 0]);
    assertEqual(b.ucs2Slice(0, 4), 'AB');
});

test('base64Slice - basic', () => {
    const b = Buffer.from('Hello');
    assertEqual(b.base64Slice(0, 5), 'SGVsbG8=');
});

// ===== Buffer iteration =====

test('for...of iteration', () => {
    const b = Buffer.from([1, 2, 3]);
    const vals = [];
    for (const v of b) vals.push(v);
    assertEqual(vals.length, 3);
    assertEqual(vals[0], 1);
    assertEqual(vals[1], 2);
    assertEqual(vals[2], 3);
});

// ===== Buffer.values() / keys() / entries() =====

test('values()', () => {
    const b = Buffer.from([1, 2]);
    const vals = [...b.values()];
    assertEqual(vals[0], 1);
    assertEqual(vals[1], 2);
});

test('keys()', () => {
    const b = Buffer.from([10, 20]);
    const keys = [...b.keys()];
    assertEqual(keys[0], 0);
    assertEqual(keys[1], 1);
});

test('entries()', () => {
    const b = Buffer.from([10, 20]);
    const entries = [...b.entries()];
    assertEqual(entries[0][0], 0);
    assertEqual(entries[0][1], 10);
});

// ===== Buffer.equals() =====

test('equals - same', () => {
    const a = Buffer.from([1, 2, 3]);
    const b = Buffer.from([1, 2, 3]);
    assertEqual(a.equals(b), true);
});

test('equals - different', () => {
    const a = Buffer.from([1, 2, 3]);
    const b = Buffer.from([1, 2, 4]);
    assertEqual(a.equals(b), false);
});

test('equals - Uint8Array accepted', () => {
    const a = Buffer.from([1, 2, 3]);
    const b = new Uint8Array([1, 2, 3]);
    assertEqual(a.equals(b), true);
});

test('equals - non-Uint8Array throws', () => {
    let threw = false;
    try { Buffer.from([1]).equals(new Uint16Array([1])); } catch { threw = true; }
    if (!threw) throw new Error('should throw');
});

// ===== Buffer.compare() =====

test('Buffer.compare', () => {
    const a = Buffer.from([1, 2, 3]);
    const b = Buffer.from([1, 2, 4]);
    assertEqual(Buffer.compare(a, b), -1);
    assertEqual(Buffer.compare(b, a), 1);
    assertEqual(Buffer.compare(a, a), 0);
});

test('Buffer.compare - Uint8Array accepted', () => {
    assertEqual(Buffer.compare(Buffer.from([1]), new Uint8Array([1])), 0);
});

test('Buffer.compare - non-Uint8Array throws', () => {
    let threw = false;
    try { Buffer.compare(Buffer.from([1]), new Uint16Array([1])); } catch { threw = true; }
    if (!threw) throw new Error('should throw');
});

test('buffer.compare - prototype basic', () => {
    assertEqual(Buffer.from([1, 2, 3]).compare(Buffer.from([1, 2, 4])), -1);
    assertEqual(Buffer.from([1, 2, 4]).compare(Buffer.from([1, 2, 3])), 1);
    assertEqual(Buffer.from([1, 2, 3]).compare(Buffer.from([1, 2, 3])), 0);
});

test('buffer.compare - prototype ranges', () => {
    const source = Buffer.from([9, 2, 3, 9]);
    const target = Buffer.from([1, 2, 3, 4]);
    assertEqual(source.compare(target, 1, 3, 1, 3), 0);
});

test('buffer.compare - prototype invalid range throws', () => {
    let threw = false;
    try { Buffer.from([1]).compare(Buffer.from([1]), 0, 2); } catch { threw = true; }
    if (!threw) throw new Error('should throw');
});

// ===== Buffer prototype methods =====

test('fill - number', () => {
    const b = Buffer.alloc(5);
    b.fill(0xFF);
    assertEqual(b[0], 0xFF);
    assertEqual(b[4], 0xFF);
});

test('fill - string', () => {
    const b = Buffer.alloc(6);
    b.fill('abc');
    assertEqual(b.toString(), 'abcabc');
});

test('fill - with offset/end', () => {
    const b = Buffer.alloc(5);
    b.fill(0xFF, 1, 4);
    assertEqual(b[0], 0);
    assertEqual(b[1], 0xFF);
    assertEqual(b[3], 0xFF);
    assertEqual(b[4], 0);
});

test('reverse', () => {
    const b = Buffer.from([1, 2, 3]);
    b.reverse();
    assertEqual(b[0], 3);
    assertEqual(b[1], 2);
    assertEqual(b[2], 1);
});

test('swap16', () => {
    const b = Buffer.from([0x12, 0x34, 0x56, 0x78]);
    b.swap16();
    assertEqual(b[0], 0x34);
    assertEqual(b[1], 0x12);
    assertEqual(b[2], 0x78);
    assertEqual(b[3], 0x56);
});

test('swap32', () => {
    const b = Buffer.from([0x12, 0x34, 0x56, 0x78]);
    b.swap32();
    assertEqual(b[0], 0x78);
    assertEqual(b[1], 0x56);
    assertEqual(b[2], 0x34);
    assertEqual(b[3], 0x12);
});

test('swap64', () => {
    const b = Buffer.from([1, 2, 3, 4, 5, 6, 7, 8]);
    b.swap64();
    assertEqual(b[0], 8);
    assertEqual(b[7], 1);
});

// ===== JSON serialization =====

test('toJSON', () => {
    const b = Buffer.from([1, 2, 3]);
    const json = JSON.stringify(b);
    assertEqual(json, '{"type":"Buffer","data":[1,2,3]}');
});

// ===== Inspect =====

test('inspect', () => {
    const b = Buffer.from([1, 2, 3]);
    const s = b.inspect();
    // Format: <Buffer 01 02 03>
    if (!s.startsWith('<Buffer') || !s.endsWith('>')) {
        throw new Error(`unexpected inspect format: ${s}`);
    }
});

// ===== Transcode (Node.js 24) =====
// Note: transcode may not be available in all versions

test('buffer.transcode - module export', () => {
    if (typeof require === 'undefined') return;
    const bufferMod = require('buffer');
    if (typeof bufferMod.transcode !== 'function') {
        throw new Error('buffer.transcode missing');
    }
});

// ===== Buffer.poolSize =====

test('Buffer.poolSize', () => {
    if (typeof Buffer.poolSize !== 'number') throw new Error('poolSize missing');
});

// ===== atob / btoa (global) =====

test('globalThis.atob exists', () => {
    if (typeof globalThis.atob !== 'function') throw new Error('atob missing');
});

test('globalThis.btoa exists', () => {
    if (typeof globalThis.btoa !== 'function') throw new Error('btoa missing');
});

// ===== Node.js buffer module exports =====
// This test only runs in Node.js or xuan with buffer module

try {
    const bufferMod = typeof require !== 'undefined' ? require('buffer') : null;
    if (bufferMod) {
        test('buffer module - Buffer', () => {
            if (bufferMod.Buffer !== globalThis.Buffer) throw new Error('Buffer mismatch');
        });

        test('buffer module - INSPECT_MAX_BYTES', () => {
            if (typeof bufferMod.INSPECT_MAX_BYTES !== 'number') throw new Error('missing');
        });

        test('buffer module - kMaxLength', () => {
            if (typeof bufferMod.kMaxLength !== 'number') throw new Error('missing');
        });

        test('buffer module - atob/btoa', () => {
            if (typeof bufferMod.atob !== 'function') throw new Error('atob missing');
            if (typeof bufferMod.btoa !== 'function') throw new Error('btoa missing');
        });

        test('buffer module - Blob (Node 24)', () => {
            if (typeof bufferMod.Blob !== 'function') throw new Error('Blob missing');
        });

        test('buffer module - File (Node 24)', () => {
            if (typeof bufferMod.File !== 'function') throw new Error('File missing');
        });

        test('buffer module - SlowBuffer', () => {
            if (typeof bufferMod.SlowBuffer !== 'function') throw new Error('SlowBuffer missing');
        });
    }
} catch (e) {
    // Module not available, skip
}

// ===== Print results =====

console.log('\n===== Buffer Compatibility Test Results =====\n');
for (const r of results) {
    const status = r.status === 'PASS' ? '✓' : '✗';
    console.log(`${status} ${r.name}`);
    if (r.error) {
        console.log(`  Error: ${r.error}`);
    }
}

console.log(`\n===== Summary: ${passed} passed, ${failed} failed =====\n`);

if (failed > 0) {
    process.exit(1);
}
