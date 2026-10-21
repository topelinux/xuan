// Runs unchanged under Node 24 and xuan. AES vectors from NIST SP800-38A.
import {createCipheriv} from 'node:crypto';
import {DatabaseSync} from 'node:sqlite';

const core = globalThis[Symbol.for('tjs.internal.core')];
const assert = {
    strictEqual(actual, expected) {
        if (actual !== expected) throw new Error(`Expected ${expected}, got ${actual}`);
    },
    throws(fn) {
        try {
            fn();
        } catch (_) {
            return;
        }
        throw new Error('Expected exception');
    }
};
const hex = s => Buffer.from(s, 'hex');

for (const [key, expected] of [
    ['000102030405060708090a0b0c0d0e0f', '69c4e0d86a7b0430d8cdb78070b4c55a'],
    ['000102030405060708090a0b0c0d0e0f1011121314151617', 'dda97ca4864cdfe06eaf70a0ec0d7191'],
    ['000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f', '8ea2b7ca516745bfeafc49904b496089']
]) {
    const cipher = createCipheriv(`aes-${key.length * 4}-ecb`, hex(key), null).setAutoPadding(false);

    assert.strictEqual(Buffer.concat([
        cipher.update(hex('0011223344556677')),
        cipher.update(hex('8899aabbccddeeff')),
        cipher.final()
    ]).toString('hex'), expected);
    assert.throws(() => cipher.update(Buffer.alloc(1)));
    assert.throws(() => cipher.final());
}

const cipher = createCipheriv(
    'aes-128-ctr',
    hex('2b7e151628aed2a6abf7158809cf4f3c'),
    hex('f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff')
);
const plain = hex(
    '6bc1bee22e409f96e93d7e117393172aae2d8a571e03ac9c9eb76fac45af8e51' +
    '30c81c46a35ce411e5fbc1191a0a52eff69f2445df4f9b17ad2b417be66c3710'
);
const encrypted = Buffer.concat([
    cipher.update(plain.subarray(0, 7)),
    cipher.update(plain.subarray(7, 35)),
    cipher.update(plain.subarray(35)),
    cipher.final()
]);

assert.strictEqual(encrypted.toString('hex'),
    '874d6191b620e3261bef6864990db6ce9806f66b7970fdff8617187bb9fffdff' +
    '5ae4df3edbd5d35e5b4f09020db03eab1e031dda2fbe03d1792170a0f3009cee');
assert.throws(() => createCipheriv('aes-128-ctr', Buffer.alloc(15), Buffer.alloc(16)));
assert.throws(() => createCipheriv('aes-128-ctr', Buffer.alloc(16), Buffer.alloc(15)));

const bad = createCipheriv('aes-128-ecb', Buffer.alloc(16), null).setAutoPadding(false);

bad.update(Buffer.alloc(15));
assert.throws(() => bad.final());

const padded = createCipheriv('aes-128-ecb', Buffer.alloc(16), null);

assert.strictEqual(Buffer.concat([padded.update(Buffer.alloc(0)), padded.final()]).toString('hex'),
    '0143db63ee66b0cdff9f69917680151e');

console.log('Node AES ECB/CTR vectors, chunking/counter carry and padding/errors passed');

// xuan omits core.sqlite3 when built with TJS_DISABLE_SQLITE3=ON. Node has no
// tjs core object at all, so it always runs the SQLite checks below.
if (core && !core.sqlite3) {
    console.log('node:sqlite is disabled in this build; skipping SQLite checks');
} else {
    const db = new DatabaseSync(':memory:');

    db.exec('CREATE TABLE sample(id INTEGER PRIMARY KEY, t INTEGER, value REAL) STRICT');

    const insert = db.prepare('INSERT OR IGNORE INTO sample VALUES(?,?,?)');

    assert.strictEqual(insert.run(1, 1792572208099, 3.25).changes, 1);
    assert.strictEqual(insert.run(1, 2, 0).changes, 0);

    const row = db.prepare('SELECT * FROM sample WHERE id=?').get(1);

    assert.strictEqual(row.t, 1792572208099);
    assert.strictEqual(row.value, 3.25);
    assert.strictEqual(db.prepare('SELECT * FROM sample WHERE id=?').get(2), undefined);
    assert.strictEqual(db.prepare('SELECT * FROM sample').all().length, 1);
    assert.throws(() => db.prepare('SELECT no_such_column FROM sample'));

    db.close();
    assert.throws(() => insert.run(2, 1, 1));

    console.log('Node sqlite DatabaseSync/StatementSync checks passed');
}
