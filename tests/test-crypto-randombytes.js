import assert from 'tjs:assert';

const crypto = require('node:crypto');

// === randomBytes sync ===

// Basic sync usage
const buf1 = crypto.randomBytes(16);
assert.ok(buf1 instanceof Uint8Array, 'randomBytes returns Uint8Array');
assert.equal(buf1.length, 16, 'randomBytes sync length');

// Different sizes
assert.equal(crypto.randomBytes(0).length, 0, 'randomBytes zero');
assert.equal(crypto.randomBytes(1).length, 1, 'randomBytes one');
assert.equal(crypto.randomBytes(1024).length, 1024, 'randomBytes large');

// Returns Buffer when Buffer is available
const bufSync = crypto.randomBytes(8);
assert.ok(typeof bufSync.toString === 'function', 'randomBytes has toString');
assert.equal(bufSync.toString('hex').length, 16, 'randomBytes hex length');

// Randomness: two calls should differ
const a = crypto.randomBytes(16);
const b = crypto.randomBytes(16);
let allSame = true;
for (let i = 0; i < 16; i++) {
    if (a[i] !== b[i]) { allSame = false; break; }
}
assert.ok(!allSame, 'randomBytes produces different values');

// === randomBytes async ===

// Async with callback
const asyncResult1 = await new Promise((resolve, reject) => {
    crypto.randomBytes(16, (err, buf) => {
        if (err) reject(err);
        else resolve(buf);
    });
});
assert.equal(asyncResult1.length, 16, 'randomBytes async length');

// Multiple async calls
const results = await Promise.all([
    new Promise(resolve => crypto.randomBytes(8, (_, buf) => resolve(buf))),
    new Promise(resolve => crypto.randomBytes(8, (_, buf) => resolve(buf))),
    new Promise(resolve => crypto.randomBytes(8, (_, buf) => resolve(buf))),
]);
assert.equal(results.length, 3, 'randomBytes multiple async count');
for (let i = 0; i < results.length; i++) {
    assert.equal(results[i].length, 8, `randomBytes async[${i}] length`);
}

// === randomFillSync ===

const fillBuf = new Uint8Array(16);
const filled = crypto.randomFillSync(fillBuf);
assert.ok(filled === fillBuf, 'randomFillSync returns same buffer');
let allZero = true;
for (let i = 0; i < 16; i++) {
    if (fillBuf[i] !== 0) { allZero = false; break; }
}
assert.ok(!allZero, 'randomFillSync fills with non-zero values');

// With offset and size
const partialBuf = new Uint8Array(16);
// Fill only bytes 4-8
crypto.randomFillSync(partialBuf, 4, 4);
assert.equal(partialBuf[0], 0, 'randomFillSync offset untouched');
assert.equal(partialBuf[1], 0, 'randomFillSync offset untouched');
assert.equal(partialBuf[2], 0, 'randomFillSync offset untouched');
assert.equal(partialBuf[3], 0, 'randomFillSync offset untouched');

// === randomFill async ===

const asyncFillBuf = new Uint8Array(16);
await new Promise((resolve, reject) => {
    crypto.randomFill(asyncFillBuf, (err, buf) => {
        if (err) reject(err);
        else resolve(buf);
    });
});
assert.equal(asyncFillBuf.length, 16, 'randomFill async length');
let asyncFilled = false;
for (let i = 0; i < 16; i++) {
    if (asyncFillBuf[i] !== 0) { asyncFilled = true; break; }
}
assert.ok(asyncFilled, 'randomFill async fills with non-zero values');

// === Multer-like usage pattern ===

// This is how multer uses crypto.randomBytes for filename generation
const filename = crypto.randomBytes(16).toString('hex');
assert.equal(filename.length, 32, 'multer-style filename hex length');
assert.ok(/^[0-9a-f]+$/.test(filename), 'multer-style filename is hex');
