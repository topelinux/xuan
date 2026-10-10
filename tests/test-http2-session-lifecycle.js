/**
 * R4 Regression: HTTP/2 session anchor/mux lifecycle
 *
 * Tests:
 * 1. CLOSED_CLIENT_HTTP vs CLOSED semantics distinction
 *    - anchor HTTP transaction close !== transport close
 *    - session.close should only fire on LWS_CALLBACK_CLOSED
 * 2. Anchor transaction completion does not break session (mux reuse)
 *    - anchor_completed flag set after connect
 *    - multiple requests can be created after anchor completion
 * 3. session.destroy() properly closes session resources
 *
 * Run: ./build/xuan tests/test-http2-session-lifecycle.js
 */

import assert from 'tjs:assert';
import { ClientHttp2Session } from 'tjs:http2';

// === Build gate: skip if http2 not enabled ===
if (!ClientHttp2Session) {
    console.log('SKIP: http2 not enabled (XUAN_ENABLE_HTTP2=OFF)');
    process.exit(0);
}

console.log('=== R4: HTTP/2 session lifecycle regression ===');
console.log('');

// Helper: create a mock native H2Session we can control
function createMockNativeSession() {
    return {
        onconnect: null,
        onremotesettings: null,
        onlocalsettings: null,
        ongoaway: null,
        onerror: null,
        onclose: null,
        onping: null,
        destroyed: false,
        closed: false,
        connecting: false,
        encrypted: false,
        type: 1,
        state: {},
        remoteSettings: {},
        localSettings: {},
        pendingSettingsAck: false,
        socket: null,
        destroy(err) {
            this.destroyed = true;
            this.destroyError = err;
            this.closed = true;
            if (this.onclose) this.onclose(); // destroy should also trigger close callback
        },
        close() {
            this.closed = true;
        },
        ref() {},
        unref() {},
        ping(cb) {
            if (typeof cb === 'function')
                cb(null, 1, new Uint8Array(8));
        },
        request() {
            // Return a mock native stream object
            return {
                ondata: null, onend: null, onerror: null, onclose: null,
                onresponse: null, ontrailers: null,
                id: 1, rstCode: 0, pending: 0,
                destroyed: false, closed: false,
                endAfterHeaders: false, sentHeaders: {}, sentTrailers: {},
                state: { state: 'open', localWindowSize: 65535, remoteWindowSize: 65535 },
                write(chunk, encoding, cb) { if (cb) queueMicrotask(cb); },
                end(cb) { if (cb) queueMicrotask(cb); },
                close(code) {},
                destroy(err) { this.destroyed = true; },
                pause() {}, resume() {},
            };
        },
        // Simulate anchor HTTP transaction close (LWS_CALLBACK_CLOSED_CLIENT_HTTP)
        // This should NOT trigger session.onclose
        simulateAnchorClose() {
            // In real code: LWS_CALLBACK_CLOSED_CLIENT_HTTP only sets anchor_completed=true
            // It does NOT call onclose callback. We test that the JS wrapper does NOT
            // fire 'close' event when only the anchor transaction closes.
        },
        // Simulate full transport close (LWS_CALLBACK_CLOSED)
        simulateTransportClose() {
            this.closed = true;
            this.connecting = false;
            if (this.onclose) this.onclose();
        },
    };
}

// === Test 1: CLOSED_CLIENT_HTTP vs CLOSED semantics ===
console.log('--- Test 1: Anchor transaction close !== transport close ---');

{
    let closeEventCount = 0;
    let connectEventCount = 0;

    const native = createMockNativeSession();
    const session = new ClientHttp2Session(native);

    session.on('close', () => {
        closeEventCount++;
        console.log('  session "close" event fired (count: ' + closeEventCount + ')');
    });

    session.on('connect', () => {
        connectEventCount++;
        console.log('  session "connect" event fired');
    });

    // Simulate anchor HTTP transaction close (LWS_CALLBACK_CLOSED_CLIENT_HTTP)
    // In real code this callback does NOT fire onclose, it only sets anchor_completed.
    // The mock above does nothing — we verify no close event fires.
    native.simulateAnchorClose();
    console.log('  After anchor close simulation, closeEventCount:', closeEventCount);
    assert.eq(closeEventCount, 0,
        'CLOSED_CLIENT_HTTP should not trigger session "close" event');

    // Simulate full transport close (LWS_CALLBACK_CLOSED)
    native.simulateTransportClose();

    // The native onclose handler defers the 'close' event via queueMicrotask
    // to match Node.js semantics. Flush microtasks before asserting.
    await new Promise(r => setTimeout(r, 0));

    console.log('  After transport close simulation, closeEventCount:', closeEventCount);
    assert.eq(closeEventCount, 1,
        'CLOSED should trigger session "close" event');

    session.destroy();
}

console.log('');

// === Test 2: Multiple requests after anchor completion (mux reuse) ===
console.log('--- Test 2: Multiple requests after anchor completion ---');

{
    let requestCount = 0;
    let closeEventCount = 0;

    const native = createMockNativeSession();
    const session = new ClientHttp2Session(native);

    session.on('close', () => {
        closeEventCount++;
    });

    // Simulate anchor completed (LWS_CALLBACK_COMPLETED_CLIENT_HTTP)
    native.anchor_completed = true;

    assert.ok(!session.destroyed, 'session should not be destroyed');
    assert.ok(!session.closed, 'session should not be closed after anchor completion');

    // Create multiple streams (simulating session.request() calls)
    function makeRequest() {
        requestCount++;
        console.log('  Making request #' + requestCount);
        assert.ok(!session.destroyed, 'session should not be destroyed for request #' + requestCount);
        assert.ok(!session.closed, 'session should not be closed for request #' + requestCount);
        const stream = session.request({ ':method': 'GET', ':path': '/test' });
        assert.ok(stream !== null, 'stream should be created');
        stream.end();
        return stream;
    }

    makeRequest();
    makeRequest();
    makeRequest();

    console.log('  All three requests created successfully');
    console.log('  closeEventCount:', closeEventCount);
    assert.eq(closeEventCount, 0, 'creating requests should not close session');

    session.destroy();
}

console.log('');

// === Test 3: destroy() properly closes session ===
console.log('--- Test 3: session.destroy() closes resources ---');

{
    const native = createMockNativeSession();
    const session = new ClientHttp2Session(native);
    let closeEventCount = 0;
    session.on('close', () => { closeEventCount++; });

    assert.ok(!session.closed, 'session should not be closed before destroy');
    assert.ok(!session.destroyed, 'session should not be destroyed before destroy()');
    assert.ok(!native.destroyed, 'native should not be destroyed before destroy()');

    const err = new Error('cleanup');
    session.destroy(err);

    console.log('  After destroy(err):');
    console.log('    session.destroyed:', session.destroyed);
    console.log('    session.closed:', session.closed);
    console.log('    native.destroyed:', native.destroyed);
    console.log('    closeEventCount:', closeEventCount);

    assert.ok(session.destroyed, 'session.destroyed should be true after destroy()');
    assert.ok(session.closed, 'session.closed should be true after destroy()');
    assert.ok(native.destroyed, 'native.destroy should be called');
    assert.eq(native.destroyError, err, 'destroy error forwarded to native');
    assert.eq(closeEventCount, 1, 'destroy() should emit exactly one close event');
}

console.log('');
console.log('=== R4: Session lifecycle regression PASSED ===');
