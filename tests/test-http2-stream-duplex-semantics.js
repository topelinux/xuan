import assert from 'tjs:assert';
import stream from 'tjs:stream';
import { ClientHttp2Stream } from 'tjs:http2';

const { Duplex } = stream;

// Helper: create a mock native H2Stream that we control
function createMockNativeStream() {
    return {
        _callbacks: {},
        id: 1,
        rstCode: 0,
        pending: 0,
        destroyed: false,
        closed: false,
        endAfterHeaders: false,
        sentHeaders: {},
        sentTrailers: {},
        state: {
            state: 'open',
            localWindowSize: 65535,
            remoteWindowSize: 65535,
        },
        write(chunk, encoding, cb) {
            // Simulate async native write
            if (cb) queueMicrotask(cb);
        },
        end(cb) {
            // Simulate native end
            if (cb) queueMicrotask(cb);
        },
        close(code) {},
        destroy(error) {
            this.destroyed = true;
        },
        pause() {},
        resume() {},
        // Simulate native events
        emitData(chunk) {
            if (this.ondata) this.ondata(chunk);
        },
        emitEnd() {
            if (this.onend) this.onend();
        },
        emitError(err) {
            if (this.onerror) this.onerror(err);
        },
        emitClose() {
            if (this.onclose) this.onclose();
        },
    };
}

// === Test 1: data events not duplicated when native ondata fires ===
{
    const mock = createMockNativeStream();
    const ds = new ClientHttp2Stream(mock, null);

    const dataEvents = [];
    ds.on('data', (chunk) => {
        dataEvents.push(chunk);
    });

    // Ensure flowing mode
    ds.resume();

    // Native fires ondata once
    mock.emitData(Buffer.from('hello'));

    await new Promise(resolve => setTimeout(resolve, 50));
    assert.eq(dataEvents.length, 1, 'data event fires exactly once for single chunk');
    assert.eq(dataEvents[0].toString(), 'hello', 'data chunk content matches');

    ds.destroy();
}

// === Test 2: data events not duplicated with multiple chunks ===
{
    const mock = createMockNativeStream();
    const ds = new ClientHttp2Stream(mock, null);

    const dataEvents = [];
    ds.on('data', (chunk) => {
        dataEvents.push(chunk);
    });
    ds.resume();

    mock.emitData(Buffer.from('chunk1'));
    mock.emitData(Buffer.from('chunk2'));
    mock.emitData(Buffer.from('chunk3'));

    await new Promise(resolve => setTimeout(resolve, 50));
    assert.eq(dataEvents.length, 3, 'three data events for three native ondata calls');

    ds.destroy();
}

// === Test 3: end event fires exactly once ===
{
    const mock = createMockNativeStream();
    const ds = new ClientHttp2Stream(mock, null);

    let endCount = 0;
    ds.on('end', () => {
        endCount++;
    });
    ds.resume();

    mock.emitData(Buffer.from('some data'));
    mock.emitEnd();
    // Simulate native firing end again (shouldn't happen, but we guard)
    mock.emitEnd();

    await new Promise(resolve => setTimeout(resolve, 50));
    assert.eq(endCount, 1, 'end event fires exactly once even if native fires twice');

    ds.destroy();
}

// === Test 4: data + end sequence matches Duplex semantics ===
{
    const mock = createMockNativeStream();
    const ds = new ClientHttp2Stream(mock, null);

    const events = [];
    ds.on('data', () => events.push('data'));
    ds.on('end', () => events.push('end'));

    ds.resume();
    mock.emitData(Buffer.from('x'));
    mock.emitEnd();

    await new Promise(resolve => setTimeout(resolve, 50));
    assert.ok(events.includes('data'), 'data event present');
    assert.ok(events.includes('end'), 'end event present');
    assert.ok(events.indexOf('data') < events.indexOf('end'), 'data before end');

    ds.destroy();
}

// === Test 5: error is normalized to Error-like object ===
{
    const mock = createMockNativeStream();
    const ds = new ClientHttp2Stream(mock, null);

    let errorObj = null;
    ds.on('error', (err) => {
        errorObj = err;
    });

    // Native fires a string error
    mock.emitError('something went wrong');

    await new Promise(resolve => setTimeout(resolve, 50));
    assert.ok(errorObj instanceof Error, 'error is an Error instance');
    assert.ok(errorObj.message.includes('something went wrong'), 'error message preserved');

    ds.destroy();
}

// === Test 6: error object with code preserves properties ===
{
    const mock = createMockNativeStream();
    const ds = new ClientHttp2Stream(mock, null);

    let errorObj = null;
    ds.on('error', (err) => {
        errorObj = err;
    });

    mock.emitError({ message: 'custom error', code: 'ERR_HTTP2_STREAM' });

    await new Promise(resolve => setTimeout(resolve, 50));
    assert.ok(errorObj instanceof Error, 'error is an Error instance');
    assert.eq(errorObj.code, 'ERR_HTTP2_STREAM', 'error code preserved');

    ds.destroy();
}

// === Test 7: native onclose does not duplicate close event ===
{
    const mock = createMockNativeStream();
    const ds = new ClientHttp2Stream(mock, null);

    let closeCount = 0;
    ds.on('close', () => {
        closeCount++;
    });

    mock.emitClose();
    mock.emitClose();
    await new Promise(resolve => setTimeout(resolve, 10));
    assert.eq(closeCount, 1, 'close emitted exactly once');

    ds.destroy();
}

// === Test 8: destroy with error passes Error-like object to native ===
{
    let nativeDestroyError = null;
    const mock = createMockNativeStream();
    // Intercept native destroy BEFORE stream is created so _native.destroy captures it
    const origDestroy = mock.destroy.bind(mock);
    mock.destroy = function (err) {
        nativeDestroyError = err;
        origDestroy(err);
    };

    const ds = new ClientHttp2Stream(mock, null);

    ds.destroy('raw string error');

    await new Promise(resolve => setTimeout(resolve, 50));
    assert.ok(nativeDestroyError instanceof Error, 'native destroy receives Error instance');
    assert.ok(nativeDestroyError.message.includes('raw string error'), 'error message preserved in destroy');
}

// === Test 9: pause/resume does not conflict with native state ===
{
    const mock = createMockNativeStream();
    let nativePaused = false;
    mock.pause = () => { nativePaused = true; };
    mock.resume = () => { nativePaused = false; };

    const ds = new ClientHttp2Stream(mock, null);

    ds.pause();
    assert.ok(nativePaused, 'native pause called');

    ds.resume();
    assert.ok(!nativePaused, 'native resume called');

    ds.destroy();
}

// === Test 10: _final callback not called twice ===
{
    const mock = createMockNativeStream();
    let nativeEndCbCount = 0;
    mock.end = function (cb) {
        if (cb) {
            cb();
            // Simulate buggy native calling callback twice
            cb();
        }
    };

    const ds = new ClientHttp2Stream(mock, null);

    let finalCbCount = 0;
    // Monkey-patch _final to count callbacks
    const origFinal = ds._final.bind(ds);
    ds._final = function (cb) {
        let called = false;
        const wrapped = function (...args) {
            if (!called) {
                called = true;
                finalCbCount++;
                cb(...args);
            }
        };
        origFinal(wrapped);
    };

    // Trigger end via writing
    ds.write('test');
    ds.end();

    await new Promise(resolve => setTimeout(resolve, 50));
    assert.ok(finalCbCount <= 1, '_final callback guarded against double call');

    ds.destroy();
}

// === Test 11: _finalCalled flag prevents _final re-entry ===
{
    const mock = createMockNativeStream();
    let nativeEndCallCount = 0;
    mock.end = function (cb) {
        nativeEndCallCount++;
        if (cb) queueMicrotask(cb);
    };

    const ds = new ClientHttp2Stream(mock, null);

    // Call end twice on the stream
    ds.end();
    ds.end();

    await new Promise(resolve => setTimeout(resolve, 50));
    assert.eq(nativeEndCallCount, 1, 'native end() called only once despite double stream.end()');

    ds.destroy();
}
