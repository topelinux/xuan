import assert from 'tjs:assert';
import { gunzipSync, createGunzipStream, gunzipStreamPush, gunzipStreamFinish } from 'tjs:zlib';

const gzipData = new Uint8Array([
    0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x02, 0xff, 0xcb, 0x48, 0xcd, 0xc9, 0xc9, 0x57,
    0x48, 0x2b, 0xca, 0xcf, 0x55, 0x28, 0xc9, 0x2a,
    0x56, 0xa8, 0xca, 0xc9, 0x4c, 0x52, 0x48, 0x2f,
    0xcd, 0xab, 0xca, 0x2c, 0x08, 0xae, 0xcc, 0x4b,
    0xe6, 0x02, 0x00, 0xc1, 0x12, 0x3f, 0x50, 0x1f,
    0x00, 0x00, 0x00
]);

const plain = new TextDecoder().decode(gunzipSync(gzipData));
assert.equal(plain, 'hello from tjs zlib gunzipSync\n');

const corrupted = gzipData.slice();
corrupted[corrupted.length - 1] ^= 0xff;
assert.throws(() => gunzipSync(corrupted), TypeError);

const stream = createGunzipStream();
const outParts = [];
for (let i = 0; i < gzipData.length; i += 3) {
    const chunk = gzipData.subarray(i, i + 3);
    outParts.push(gunzipStreamPush(stream, chunk));
}
outParts.push(gunzipStreamFinish(stream));

let total = 0;
for (const part of outParts) total += part.length;
const merged = new Uint8Array(total);
let offset = 0;
for (const part of outParts) {
    merged.set(part, offset);
    offset += part.length;
}
assert.equal(new TextDecoder().decode(merged), 'hello from tjs zlib gunzipSync\n');
