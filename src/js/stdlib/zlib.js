const core = globalThis[Symbol.for('tjs.internal.core')];

const Z_SYNC_FLUSH = 2;
const Z_DEFAULT_WINDOWBITS = 15;

class DummyZlib {
    on() { return this; }
    once() { return this; }
    removeListener() { return this; }
    write() { throw new Error('zlib is not supported in this build'); }
    flush(_kind, cb) { if (typeof cb === 'function') cb(); }
    close() {}
}

function createDeflateRaw() {
    return new DummyZlib();
}

function createInflateRaw() {
    return new DummyZlib();
}

function deflateRaw() {
    throw new Error('zlib is not supported in this build');
}

function inflateRaw() {
    throw new Error('zlib is not supported in this build');
}

function gunzipSync(buffer) {
    return core.gunzipSync(buffer);
}

function createGunzipStream() {
    return core.gunzipStreamCreate();
}

function gunzipStreamPush(stream, chunk) {
    return core.gunzipStreamPush(stream, chunk);
}

function gunzipStreamFinish(stream) {
    return core.gunzipStreamFinish(stream);
}

export {
    Z_SYNC_FLUSH,
    Z_DEFAULT_WINDOWBITS,
    createDeflateRaw,
    createInflateRaw,
    deflateRaw,
    inflateRaw,
    gunzipSync,
    createGunzipStream,
    gunzipStreamPush,
    gunzipStreamFinish,
};

export default {
    Z_SYNC_FLUSH,
    Z_DEFAULT_WINDOWBITS,
    createDeflateRaw,
    createInflateRaw,
    deflateRaw,
    inflateRaw,
    gunzipSync,
    createGunzipStream,
    gunzipStreamPush,
    gunzipStreamFinish,
};
