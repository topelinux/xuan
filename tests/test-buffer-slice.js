import assert from 'tjs:assert';

// Test Buffer slice methods for Node.js compatibility
// These methods are required by busboy/streamsearch for multipart form parsing

const { Buffer } = globalThis;

// === latin1Slice ===

// Basic latin1Slice test
const latin1Buf = Buffer.from('Hello World', 'utf8');
assert.equal(latin1Buf.latin1Slice(0, 5), 'Hello', 'latin1Slice basic');
assert.equal(latin1Buf.latin1Slice(6, 11), 'World', 'latin1Slice mid-section');
assert.equal(latin1Buf.latin1Slice(0, 11), 'Hello World', 'latin1Slice full');
assert.equal(latin1Buf.latin1Slice(5, 5), '', 'latin1Slice empty range');

// latin1Slice with negative indices
assert.equal(latin1Buf.latin1Slice(-5), 'World', 'latin1Slice negative start');
assert.equal(latin1Buf.latin1Slice(0, -6), 'Hello', 'latin1Slice negative end');
assert.equal(latin1Buf.latin1Slice(-5, -1), 'Worl', 'latin1Slice both negative');

// latin1Slice with bytes > 127 (Latin-1 range)
const latin1Extended = Buffer.from(new Uint8Array([ 0xC9, 0xE9, 0xF1, 0xFC ]));
assert.equal(latin1Extended.latin1Slice(0, 4).length, 4, 'latin1Slice extended length');
assert.equal(latin1Extended.latin1Slice(0, 4).charCodeAt(0), 0xC9, 'latin1Slice char code');
assert.equal(latin1Extended.toString('latin1'), 'Ééñü', 'toString latin1 extended bytes');
assert.equal(Buffer.from('Ééñü', 'latin1').toString('hex'), 'c9e9f1fc', 'Buffer.from latin1 extended string');

// Multer/busboy exposes non-ASCII multipart filenames as Latin-1 strings.
// Applications commonly recover the browser's UTF-8 bytes with this round trip.
const multipartName = 'sys-config-æµè¯.json';
const decodedName = Buffer.from(multipartName, 'latin1').toString('utf8');
assert.equal(decodedName, 'sys-config-测试.json', 'multipart filename latin1-to-utf8 recovery');
assert.equal(Buffer.from(decodedName, 'utf8').toString('latin1'), multipartName, 'multipart filename utf8-to-latin1 round trip');

// === utf8Slice ===

const utf8Buf = Buffer.from('你好世界', 'utf8');
assert.equal(utf8Buf.utf8Slice(0, utf8Buf.length), '你好世界', 'utf8Slice Chinese');

const utf8Simple = Buffer.from('Hello', 'utf8');
assert.equal(utf8Simple.utf8Slice(0, 5), 'Hello', 'utf8Slice ASCII');

// === ucs2Slice ===

// UCS-2: "AB" = 0x0041 0x0042 in UTF-16 LE = bytes [65, 0, 66, 0]
const ucs2Buf = Buffer.from(new Uint8Array([ 65, 0, 66, 0, 67, 0 ]));
const ucs2Result = ucs2Buf.ucs2Slice(0, 6);
assert.equal(ucs2Result.length, 3, 'ucs2Slice length');
assert.equal(ucs2Result.charCodeAt(0), 65, 'ucs2Slice char A');
assert.equal(ucs2Result.charCodeAt(1), 66, 'ucs2Slice char B');
assert.equal(ucs2Result.charCodeAt(2), 67, 'ucs2Slice char C');

// === base64Slice ===

// "Hello" in base64 = "SGVsbG8="
const base64Buf = Buffer.from('Hello', 'utf8');
assert.equal(base64Buf.base64Slice(0, 5), 'SGVsbG8=', 'base64Slice Hello');

const base64Empty = Buffer.alloc(0);
assert.equal(base64Empty.base64Slice(0, 0), '', 'base64Slice empty');

// === Busboy-like multipart header parsing ===

// This is the exact pattern busboy uses in multipart.js:63
const headerBuf = Buffer.from('Content-Disposition: form-data; name="file"', 'utf8');
const headerName = headerBuf.latin1Slice(0, 19);
assert.equal(headerName, 'Content-Disposition', 'busboy header parsing');

const headerValue = headerBuf.latin1Slice(21, 44);
assert.equal(headerValue, 'form-data; name="file"', 'busboy header value');

// === Edge cases ===

const smallBuf = Buffer.from('X');
assert.equal(smallBuf.latin1Slice(0, 1), 'X', 'single byte latin1Slice');
assert.equal(smallBuf.utf8Slice(0, 1), 'X', 'single byte utf8Slice');
assert.equal(smallBuf.base64Slice(0, 1), 'WA==', 'single byte base64Slice');

// Out of bounds (should clamp)
const clampBuf = Buffer.from('abc');
assert.equal(clampBuf.latin1Slice(0, 100), 'abc', 'latin1Slice end out of bounds');
assert.equal(clampBuf.latin1Slice(-100, 3), 'abc', 'latin1Slice start out of bounds');
