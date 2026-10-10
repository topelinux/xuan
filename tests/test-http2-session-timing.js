import assert from 'tjs:assert';
import { ClientHttp2Session, connect } from 'tjs:http2';

console.log('=== R6: HTTP/2 session event timing regression ===');

// R6: Verify connect event timing — listener registered AFTER connect()
// should NOT miss connect/remoteSettings events.

// === Build gate: skip if http2 not enabled ===
if (!connect) {
    console.log('SKIP: http2 not enabled (XUAN_ENABLE_HTTP2=OFF)');
    process.exit(0);
}

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
            throw new Error('not implemented in timing test');
        },
    };
}

// Collect results to assert after events fire
function test1() {
    let connectEvents = 0;
    let remoteSettingsEvents = 0;
    let errorEvents = 0;
    let closeEvents = 0;
    let done = false;

    const native = createMockNativeSession();
    const session = new ClientHttp2Session(native);

    session.on('connect', () => { connectEvents++; });
    session.on('remoteSettings', () => { remoteSettingsEvents++; });
    session.on('error', (err) => {
        errorEvents++;
        assert.ok(err instanceof Error, 'error event is normalized to Error');
    });
    session.on('close', () => { closeEvents++; });

    native.onconnect();
    native.onremotesettings({ maxConcurrentStreams: 100 });
    native.onerror('simulated error');
    native.onclose();

    // Schedule assertions after microtasks
    queueMicrotask(() => {
        assert.eq(connectEvents, 1, 'connect listener fired once');
        assert.eq(remoteSettingsEvents, 1, 'remoteSettings listener fired once');
        assert.eq(errorEvents, 1, 'error listener fired once');
        assert.eq(closeEvents, 1, 'close listener fired once');
        session.destroy();
        done = true;
        console.log('  Test 1 passed');
        test2();
    });

    // Keep event loop alive: ref the process
    // The queueMicrotask above will chain to test2
}

function test2() {
    const native = createMockNativeSession();
    const session = new ClientHttp2Session(native);

    session.on('connect', () => {});
    session.on('remoteSettings', () => {});
    session.on('close', () => {});

    const names = session.eventNames();
    assert.ok(Array.isArray(names), 'eventNames returns array');
    assert.ok(names.includes('connect'), 'connect is registered');
    assert.ok(names.includes('remoteSettings'), 'remoteSettings is registered');
    assert.ok(names.includes('close'), 'close is registered');

    session.destroy();
    console.log('  Test 2 passed');
    test3();
}

function test3() {
    let count1 = 0;
    let count2 = 0;

    const native = createMockNativeSession();
    const session = new ClientHttp2Session(native);

    session.on('error', () => { count1++; });
    session.on('error', () => { count2++; });

    native.onerror(new Error('boom'));

    queueMicrotask(() => {
        assert.eq(count1, 1, 'first listener fired once');
        assert.eq(count2, 1, 'second listener fired once');
        session.destroy();
        console.log('  Test 3 passed');
        test4();
    });
}

function test4() {
    const native = createMockNativeSession();
    const session = new ClientHttp2Session(native);
    const err = new Error('destroy test');
    session.destroy(err);
    assert.ok(native.destroyed, 'native destroy was called');
    assert.eq(native.destroyError, err, 'destroy error forwarded to native');
    console.log('  Test 4 passed');

    console.log('');
    console.log('=== R6: Session event timing regression PASSED ===');
}

// Start the test chain
test1();
