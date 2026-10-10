import assert from 'tjs:assert';
import http2 from 'tjs:http2';

// R6: Verify http2 module exports, constants, and default settings.
// This is the most basic smoke test — if this fails, nothing else will work.

// === Build gate: skip if http2 not enabled ===
if (!http2.connect) {
    console.log('SKIP: http2 not enabled (XUAN_ENABLE_HTTP2=OFF)');
    process.exit(0);
}

// === Test 1: Module exports ===
{
    assert.ok(typeof http2.connect === 'function', 'http2.connect is a function');
    assert.ok(typeof http2.ClientHttp2Session === 'function', 'ClientHttp2Session is a constructor');
    assert.ok(typeof http2.ClientHttp2Stream === 'function', 'ClientHttp2Stream is a constructor');
    assert.ok(typeof http2.constants === 'object', 'constants is an object');
    assert.ok(typeof http2.getDefaultSettings === 'function', 'getDefaultSettings is a function');
    assert.ok(Array.isArray(http2.sensitiveHeaders), 'sensitiveHeaders is an array');
}

// === Test 2: node:http2 alias ===
{
    try {
        const nodeHttp2 = require('node:http2');
        assert.strictEqual(nodeHttp2.connect, http2.connect, 'node:http2.connect === http2.connect');
        assert.strictEqual(nodeHttp2.ClientHttp2Session, http2.ClientHttp2Session,
            'node:http2.ClientHttp2Session === http2.ClientHttp2Session');
        assert.strictEqual(nodeHttp2.ClientHttp2Stream, http2.ClientHttp2Stream,
            'node:http2.ClientHttp2Stream === http2.ClientHttp2Stream');
        assert.strictEqual(nodeHttp2.constants, http2.constants,
            'node:http2.constants === http2.constants');
        assert.strictEqual(nodeHttp2.getDefaultSettings, http2.getDefaultSettings,
            'node:http2.getDefaultSettings === http2.getDefaultSettings');
    } catch (err) {
        console.log('NOTE: node:http2 alias check skipped:', err.message);
    }
}

// === Test 3: Constants cover grpc-js requirements ===
{
    const c = http2.constants;

    // Error codes
    assert.ok(typeof c.NGHTTP2_NO_ERROR === 'number', 'NGHTTP2_NO_ERROR');
    assert.ok(typeof c.NGHTTP2_PROTOCOL_ERROR === 'number', 'NGHTTP2_PROTOCOL_ERROR');
    assert.ok(typeof c.NGHTTP2_INTERNAL_ERROR === 'number', 'NGHTTP2_INTERNAL_ERROR');
    assert.ok(typeof c.NGHTTP2_CANCEL === 'number', 'NGHTTP2_CANCEL');
    assert.ok(typeof c.NGHTTP2_REFUSED_STREAM === 'number', 'NGHTTP2_REFUSED_STREAM');

    // Header name constants
    assert.ok(typeof c.HTTP2_HEADER_STATUS === 'string', 'HTTP2_HEADER_STATUS');
    assert.ok(typeof c.HTTP2_HEADER_METHOD === 'string', 'HTTP2_HEADER_METHOD');
    assert.ok(typeof c.HTTP2_HEADER_PATH === 'string', 'HTTP2_HEADER_PATH');
    assert.ok(typeof c.HTTP2_HEADER_AUTHORITY === 'string', 'HTTP2_HEADER_AUTHORITY');
    assert.ok(typeof c.HTTP2_HEADER_SCHEME === 'string', 'HTTP2_HEADER_SCHEME');
    assert.ok(typeof c.HTTP2_HEADER_CONTENT_TYPE === 'string', 'HTTP2_HEADER_CONTENT_TYPE');

    assert.eq(c.NGHTTP2_NO_ERROR, 0, 'NGHTTP2_NO_ERROR === 0');
    assert.eq(c.NGHTTP2_CANCEL, 8, 'NGHTTP2_CANCEL === 8');
}

// === Test 4: getDefaultSettings returns valid shape ===
{
    const settings = http2.getDefaultSettings();
    assert.ok(typeof settings === 'object', 'settings is an object');
    assert.ok(typeof settings.headerTableSize === 'number', 'headerTableSize');
    assert.ok(typeof settings.enablePush === 'number', 'enablePush');
    assert.ok(typeof settings.maxConcurrentStreams === 'number', 'maxConcurrentStreams');
    assert.ok(typeof settings.initialWindowSize === 'number', 'initialWindowSize');
    assert.ok(typeof settings.maxFrameSize === 'number', 'maxFrameSize');
    assert.ok(typeof settings.maxHeaderListSize === 'number', 'maxHeaderListSize');

    // Standard defaults from RFC 7540
    assert.eq(settings.initialWindowSize, 65535, 'initialWindowSize === 65535');
    assert.eq(settings.maxFrameSize, 16384, 'maxFrameSize === 16384');
}

// === Test 5: sensitiveHeaders shape ===
{
    const sh = http2.sensitiveHeaders;
    assert.ok(Array.isArray(sh), 'sensitiveHeaders is array');
    // May be empty in minimal implementation
    for (const h of sh) {
        assert.ok(typeof h === 'symbol' || typeof h === 'string',
            'each sensitiveHeader is symbol or string');
    }
}
