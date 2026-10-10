import assert from 'tjs:assert';
import { createHash } from 'crypto';
import crypto from 'node:crypto';

const h1 = createHash('sha256').update('abc').digest();
assert.eq(typeof h1, 'string');
assert.ok(h1.length > 0);

const h2 = crypto.createHash('sha256').update('abc').digest();
assert.eq(h1, h2);

const cjsCryptoNs = require('crypto');
const cjsCrypto = cjsCryptoNs.default ?? cjsCryptoNs;
assert.eq(cjsCrypto.createHash('sha256').update('abc').digest(), h1);
