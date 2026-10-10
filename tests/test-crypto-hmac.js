import assert from 'tjs:assert';
import crypto, { createHmac } from 'node:crypto';

const message = 'The quick brown fox jumps over the lazy dog';

assert.eq(
    createHmac('sha256', 'key').update(message).digest('hex'),
    'f7bc83f430538424b13298e6aa6fb143ef4d59a14946175997479dbc2d1a3cd8',
    'sha256 hmac hex matches Node.js'
);

assert.eq(
    crypto.createHmac('md5', 'key').update(message).digest(),
    '80070713463e7749b90c2dc24911e275',
    'default hmac digest stays hex-compatible with existing xuan hashing'
);

assert.eq(
    crypto.createHmac('sha512-256', 'key').update(message).digest('hex'),
    '7fb65e03577da9151a1016e9c2e514d4d48842857f13927f348588173dca6d89',
    'hyphenated Node.js algorithm alias works'
);

assert.eq(
    crypto.createHmac('sha3-256', 'key').update('abc').digest('hex'),
    '09b6dbab8d11795ca7c8d82f1cf91682013c7cb980abbb25473be4ae7f7b5683',
    'sha3 hmac works through native mbedtls'
);

assert.eq(
    crypto.createHmac('sha256', Buffer.from('secret')).update(Buffer.from('abc')).digest('base64'),
    'mUba1OAOkT/Ivo5dP34RCkqegy+D+wnDRShdeGONig4=',
    'buffer input and base64 digest work'
);

assert.eq(
    crypto.createHmac('sha256', '6b6579', { encoding: 'hex' }).update('abc').digest('hex'),
    '9c196e32dc0175f86f4b1cb89289d6619de6bee699e4c378e68309ed97a1a6ab',
    'string key encoding option works'
);

const cjsCryptoNs = require('crypto');
const cjsCrypto = cjsCryptoNs.default ?? cjsCryptoNs;
assert.eq(
    cjsCrypto.createHmac('sha256', 'key').update('abc').digest('hex'),
    '9c196e32dc0175f86f4b1cb89289d6619de6bee699e4c378e68309ed97a1a6ab',
    'CommonJS crypto createHmac works'
);

assert.ok(
    crypto.timingSafeEqual(Buffer.from('same'), Buffer.from('same')),
    'timingSafeEqual returns true for equal buffers'
);
assert.ok(
    !crypto.timingSafeEqual(Buffer.from('same'), Buffer.from('diff')),
    'timingSafeEqual returns false for different same-length buffers'
);
assert.throws(
    () => crypto.timingSafeEqual(Buffer.from('short'), Buffer.from('longer')),
    RangeError,
    'timingSafeEqual throws for different lengths'
);
