// Synchronous Node-compatible AES-CTR decryption; NIST SP800-38A F.5 vectors.
import assert from 'node:assert';
import crypto from 'node:crypto';
const plain = Buffer.from('6bc1bee22e409f96e93d7e117393172aae2d8a571e03ac9c9eb76fac45af8e51', 'hex');
const iv = Buffer.from('f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff', 'hex');
const vectors = [
    ['2b7e151628aed2a6abf7158809cf4f3c', '874d6191b620e3261bef6864990db6ce9806f66b7970fdff8617187bb9fffdff'],
    ['8e73b0f7da0e6452c810f32b809079e562f8ead2522c6b7b', '1abc932417521ca24f2b0459fe7e6e0b090339ec0aa6faefd5ccc2c6f4ce8e94'],
    ['603deb1015ca71be2b73aef0857d77811f352c073b6108d72d9810a30914dff4', '601ec313775789a5b7a7f504bbf3d228f443e3ca4d62b59aca84e990cacaf5c5']
];
for (const [keyHex, cipherHex] of vectors) {
    const key = Buffer.from(keyHex, 'hex'), data = Buffer.from(cipherHex, 'hex');
    const algorithm = 'aes-' + key.length * 8 + '-ctr';
    for (const split of [0, 1, 15, 16, 17, 31, 32]) {
        const d = crypto.createDecipheriv(algorithm, key, iv);
        const result = Buffer.concat([d.update(data.slice(0, split)), d.update(data.slice(split)), d.final()]);
        assert.strictEqual(result.toString('hex'), plain.toString('hex'));
        assert.throws(() => d.final());
        assert.throws(() => d.update(Buffer.alloc(0)));
    }
    const d = crypto.createDecipheriv(algorithm.toUpperCase(), key, iv);
    assert.strictEqual(d.update(data.toString('hex'), 'hex', 'hex') + d.final('hex'), plain.toString('hex'));
}
assert.throws(() => crypto.createDecipheriv('aes-128-ctr', Buffer.alloc(15), iv));
assert.throws(() => crypto.createDecipheriv('aes-128-ctr', Buffer.alloc(16), Buffer.alloc(15)));
const empty = crypto.createDecipheriv('aes-128-ctr', Buffer.alloc(16), iv);
assert.strictEqual(empty.update(Buffer.alloc(0)).length, 0);
assert.strictEqual(empty.final().length, 0);
console.log('crypto CTR: NIST AES128/192/256 decryption, chunk boundaries, encodings and finalization passed');
