import assert from 'tjs:assert';
import { ClientHttp2Stream, ClientHttp2Session } from 'tjs:http2';

// R6: Verify stream data/end/close events don't duplicate.
//
// grpc-js subchannel-call.js relies on:
//   stream.on('data', ...)
//   stream.on('end', ...)
//   stream.on('close', ...)
//   stream.on('error', ...)
// Duplication or missing events cause grpc-js to hang or crash.

// === Build gate: skip if http2 not enabled ===
if (!ClientHttp2Stream) {
    console.log('SKIP: http2 not enabled (XUAN_ENABLE_HTTP2=OFF)');
    process.exit(0);
}

// Helper: create a mock native H2Stream we can control
function createMockNativeStream() {
    return {
        ondata: null, onend: null, onerror: null, onclose: null,
        onresponse: null, ontrailers: null,
        id: 1,
        rstCode: 0,
        pending: 0,
        destroyed: false,
        closed: false,
        endAfterHeaders: false,
        sentHeaders: {},
        sentTrailers: {},
        state: { state: 'open', localWindowSize: 65535, remoteWindowSize: 65535 },
        write(chunk, encoding, cb) { if (cb) queueMicrotask(cb); },
        end(cb) { if (cb) queueMicrotask(cb); },
        close(code) {},
        destroy(err) { this.destroyed = true; if (this.onclose) this.onclose(); },
        pause() {},
        resume() {},
        emitResponse(headers = {}, flags = 0) { if (this.onresponse) this.onresponse(headers, flags, undefined); },
        emitTrailers(headers = {}, flags = 0) { if (this.ontrailers) this.ontrailers(headers, flags); },
        emitData(chunk) { if (this.ondata) this.ondata(chunk); },
        emitEnd() { if (this.onend) this.onend(); },
        emitError(err) { if (this.onerror) this.onerror(err); },
        emitClose() { if (this.onclose) this.onclose(); },
    };
}

// Helper: create a minimal mock session
function createMockSession() {
    // We can't set native properties directly (they're getters).
    // Instead, just return a JS object that acts as a session proxy.
    return {
        _native: null,
        closed: false,
        destroyed: false,
        connect_host: 'localhost',
        connect_port: 8080,
        connect_ssl: false,
        destroy() { this.destroyed = true; },
    };
}

// === Test 1: Single data chunk triggers exactly one 'data' event ===
{
    const mock = createMockNativeStream();
    const session = createMockSession();
    const stream = new ClientHttp2Stream(mock, session);

    const dataEvents = [];
    stream.on('data', (chunk) => { dataEvents.push(chunk); });
    stream.resume(); // Enter flowing mode

    // Native fires ondata once
    mock.emitData(Buffer.from('hello'));

    await new Promise(resolve => setTimeout(resolve, 50));
    assert.eq(dataEvents.length, 1, 'data fires exactly once for single chunk');
    assert.eq(dataEvents[0].toString(), 'hello', 'data content matches');

    stream.destroy();
    if (session) session.destroy();
}

// === Test 2: Multiple data chunks fire in order, no duplication ===
{
    const mock = createMockNativeStream();
    const session = createMockSession();
    const stream = new ClientHttp2Stream(mock, session);

    const dataEvents = [];
    stream.on('data', (chunk) => { dataEvents.push(chunk.toString()); });
    stream.resume();

    mock.emitData(Buffer.from('chunk-1'));
    mock.emitData(Buffer.from('chunk-2'));
    mock.emitData(Buffer.from('chunk-3'));

    await new Promise(resolve => setTimeout(resolve, 50));
    assert.eq(dataEvents.length, 3, '3 data events for 3 native ondata calls');
    assert.eq(dataEvents.join('|'), 'chunk-1|chunk-2|chunk-3', 'data events in order');

    stream.destroy();
    if (session) session.destroy();
}

// === Test 3: 'end' fires exactly once ===
{
    const mock = createMockNativeStream();
    const session = createMockSession();
    const stream = new ClientHttp2Stream(mock, session);

    let endCount = 0;
    stream.on('end', () => { endCount++; });
    stream.resume();

    mock.emitData(Buffer.from('data'));
    mock.emitEnd();
    // Simulate buggy native firing end again
    mock.emitEnd();

    await new Promise(resolve => setTimeout(resolve, 50));
    assert.eq(endCount, 1, 'end fires exactly once even if native fires twice');

    stream.destroy();
    if (session) session.destroy();
}

// === Test 4: data → end ordering ===
{
    const mock = createMockNativeStream();
    const session = createMockSession();
    const stream = new ClientHttp2Stream(mock, session);

    const eventOrder = [];
    stream.on('data', () => eventOrder.push('data'));
    stream.on('end', () => eventOrder.push('end'));
    stream.resume();

    mock.emitData(Buffer.from('x'));
    mock.emitEnd();

    await new Promise(resolve => setTimeout(resolve, 50));
    assert.ok(eventOrder.includes('data'), 'data event present');
    assert.ok(eventOrder.includes('end'), 'end event present');
    assert.ok(eventOrder.indexOf('data') < eventOrder.indexOf('end'),
        'data before end');

    stream.destroy();
    if (session) session.destroy();
}

// === Test 5: 'close' event fires on destroy ===
{
    const mock = createMockNativeStream();
    const session = createMockSession();
    const stream = new ClientHttp2Stream(mock, session);

    let closeCount = 0;
    stream.on('close', () => { closeCount++; });

    stream.destroy();
    await new Promise(resolve => setTimeout(resolve, 50));
    assert.ok(closeCount >= 1, 'close fires at least once on destroy');

    if (session) session.destroy();
}

// === Test 6: 'error' event fires with Error-like object ===
{
    const mock = createMockNativeStream();
    const session = createMockSession();
    const stream = new ClientHttp2Stream(mock, session);

    let errorObj = null;
    stream.on('error', (err) => { errorObj = err; });

    // Native fires a string error
    mock.emitError('something went wrong');

    await new Promise(resolve => setTimeout(resolve, 50));
    assert.ok(errorObj instanceof Error, 'error is an Error instance');

    stream.destroy();
    if (session) session.destroy();
}

// === Test 7: pause/resume doesn't conflict with native ===
{
    const mock = createMockNativeStream();
    let nativePaused = false;
    mock.pause = () => { nativePaused = true; };
    mock.resume = () => { nativePaused = false; };

    const session = createMockSession();
    const stream = new ClientHttp2Stream(mock, session);

    stream.pause();
    assert.ok(nativePaused, 'native pause called');

    stream.resume();
    assert.ok(!nativePaused, 'native resume called');

    stream.destroy();
    if (session) session.destroy();
}

// === Test 8: destroy with error passes error to native ===
{
    const mock = createMockNativeStream();
    const session = createMockSession();
    const stream = new ClientHttp2Stream(mock, session);

    let nativeDestroyError = null;
    // Intercept native destroy to capture error
    const origDestroy = mock.destroy.bind(mock);
    mock.destroy = function (err) {
        nativeDestroyError = err;
        origDestroy(err);
    };

    stream.destroy(new Error('test error'));

    await new Promise(resolve => setTimeout(resolve, 50));
    assert.ok(nativeDestroyError instanceof Error, 'error passed to native destroy');
    assert.ok(nativeDestroyError.message === 'test error', 'error message preserved');

    if (session) session.destroy();
}

// === Test 9: writable end must not emit readable 'end' before native EOF ===
{
    const mock = createMockNativeStream();
    const session = createMockSession();
    const stream = new ClientHttp2Stream(mock, session);

    let endCount = 0;
    stream.on('end', () => { endCount++; });
    stream.on('data', () => {});

    stream.end();
    await new Promise(resolve => setTimeout(resolve, 50));
    assert.eq(endCount, 0, 'stream.end() does not emit readable end before native EOF');

    mock.emitEnd();
    await new Promise(resolve => setTimeout(resolve, 50));
    assert.eq(endCount, 1, 'native EOF emits readable end exactly once');

    stream.destroy();
    if (session) session.destroy();
}

// === Test 10: response -> data -> trailers -> end ordering stays intact ===
{
    const mock = createMockNativeStream();
    const session = createMockSession();
    const stream = new ClientHttp2Stream(mock, session);

    const order = [];
    stream.on('response', () => order.push('response'));
    stream.on('data', () => order.push('data'));
    stream.on('trailers', () => order.push('trailers'));
    stream.on('end', () => order.push('end'));
    stream.on('close', () => order.push('close'));

    stream.on('data', () => {});

    stream.end();
    mock.emitResponse({ ':status': 200 }, 0);
    mock.emitData(Buffer.from([ 1, 2, 3 ]));
    mock.emitTrailers({ 'grpc-status': '0' }, 0);
    mock.emitEnd();
    mock.emitClose();

    await new Promise(resolve => setTimeout(resolve, 50));
    assert.eq(order.join('>'), 'response>data>trailers>end>close',
        'stream events keep grpc-js expected order');

    if (session) session.destroy();
}
