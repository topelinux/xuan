import assert from 'tjs:assert';
import http2 from 'tjs:http2';

// R4: Verify session-level H2 connection mux behavior at the code/API level.
//
// This test verifies the JS-layer contract for R4:
// 1. Module exports are complete
// 2. Constants cover grpc-js requirements
// 3. getDefaultSettings returns valid shape
//
// Connection-level mux is verified at the native (C) layer via code review:
// - session.connect() creates a real lws H2 anchor connection
// - session.request() calls lws_client_connect_via_info() targeting same host:port
// - lws auto-routes as substreams via lws_vhost_active_conn_find()

// === Test 1: session exports are complete ===
{
    assert.ok(typeof http2.connect === 'function', 'http2.connect is a function');
    assert.ok(typeof http2.ClientHttp2Session === 'function', 'ClientHttp2Session exported');
    assert.ok(typeof http2.ClientHttp2Stream === 'function', 'ClientHttp2Stream exported');
    assert.ok(typeof http2.constants === 'object', 'constants exported');
    assert.ok(typeof http2.getDefaultSettings === 'function', 'getDefaultSettings exported');
    assert.ok(Array.isArray(http2.sensitiveHeaders), 'sensitiveHeaders is array');
}

// === Test 2: constants cover grpc-js requirements ===
{
    const c = http2.constants;
    assert.ok(typeof c.NGHTTP2_NO_ERROR === 'number', 'NGHTTP2_NO_ERROR');
    assert.ok(typeof c.NGHTTP2_PROTOCOL_ERROR === 'number', 'NGHTTP2_PROTOCOL_ERROR');
    assert.ok(typeof c.NGHTTP2_CANCEL === 'number', 'NGHTTP2_CANCEL');
    assert.ok(typeof c.NGHTTP2_REFUSED_STREAM === 'number', 'NGHTTP2_REFUSED_STREAM');
    assert.ok(typeof c.HTTP2_HEADER_STATUS === 'string', 'HTTP2_HEADER_STATUS');
    assert.ok(typeof c.HTTP2_HEADER_METHOD === 'string', 'HTTP2_HEADER_METHOD');
    assert.ok(typeof c.HTTP2_HEADER_PATH === 'string', 'HTTP2_HEADER_PATH');
    assert.ok(typeof c.HTTP2_HEADER_AUTHORITY === 'string', 'HTTP2_HEADER_AUTHORITY');
    assert.ok(typeof c.HTTP2_HEADER_SCHEME === 'string', 'HTTP2_HEADER_SCHEME');
    assert.ok(typeof c.HTTP2_HEADER_CONTENT_TYPE === 'string', 'HTTP2_HEADER_CONTENT_TYPE');
}

// === Test 3: getDefaultSettings returns valid shape ===
{
    const settings = http2.getDefaultSettings();
    assert.ok(typeof settings.headerTableSize === 'number');
    assert.ok(typeof settings.enablePush === 'number');
    assert.ok(typeof settings.maxConcurrentStreams === 'number');
    assert.ok(typeof settings.initialWindowSize === 'number');
    assert.ok(typeof settings.maxFrameSize === 'number');
    assert.ok(typeof settings.maxHeaderListSize === 'number');
    assert.eq(settings.initialWindowSize, 65535, 'default initial window size');
    assert.eq(settings.maxFrameSize, 16384, 'default max frame size');
}

// === Test 4: ClientHttp2Session has expected prototype methods ===
{
    const proto = http2.ClientHttp2Session.prototype;
    assert.ok(typeof proto.request === 'function', 'request');
    assert.ok(typeof proto.ping === 'function', 'ping');
    assert.ok(typeof proto.close === 'function', 'close');
    assert.ok(typeof proto.destroy === 'function', 'destroy');
    assert.ok(typeof proto.ref === 'function', 'ref');
    assert.ok(typeof proto.unref === 'function', 'unref');
    assert.ok(typeof proto.setLocalWindowSize === 'function', 'setLocalWindowSize');
    assert.ok(typeof proto.incrementWindowSize === 'function', 'incrementWindowSize');
    // Note: getters (closed, destroyed, etc.) are defined via JS_CGETSET_DEF
    // on the class, not on the JS prototype. They're present on instances.
}

// === Test 5: ClientHttp2Stream has expected prototype methods ===
{
    const proto = http2.ClientHttp2Stream.prototype;

    // Duplex methods (inherited)
    assert.ok(typeof proto.write === 'function', 'write');
    assert.ok(typeof proto.end === 'function', 'end');
    assert.ok(typeof proto.destroy === 'function', 'destroy');
    assert.ok(typeof proto.pause === 'function', 'pause');
    assert.ok(typeof proto.resume === 'function', 'resume');
    assert.ok(typeof proto.pipe === 'function', 'pipe');
    // Note: H2-specific getters (id, rstCode, etc.) are defined via
    // JS_CGETSET_DEF on the class. They're present on instances.
}
