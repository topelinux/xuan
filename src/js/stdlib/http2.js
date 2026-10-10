import EventEmitter from 'tjs:events';
import Stream from 'tjs:stream';
import tls from 'tjs:tls';

const core = globalThis[Symbol.for('tjs.internal.core')];
const NativeH2Session = core.H2Session;

const {
    Duplex,
} = Stream;

const constants = Object.freeze({
    NGHTTP2_SESSION_CLIENT: 1,
    NGHTTP2_FLAG_END_STREAM: 0x1,

    NGHTTP2_NO_ERROR: 0,
    NGHTTP2_PROTOCOL_ERROR: 1,
    NGHTTP2_INTERNAL_ERROR: 2,
    NGHTTP2_FLOW_CONTROL_ERROR: 3,
    NGHTTP2_SETTINGS_TIMEOUT: 4,
    NGHTTP2_STREAM_CLOSED: 5,
    NGHTTP2_FRAME_SIZE_ERROR: 6,
    NGHTTP2_REFUSED_STREAM: 7,
    NGHTTP2_CANCEL: 8,
    NGHTTP2_COMPRESSION_ERROR: 9,
    NGHTTP2_CONNECT_ERROR: 10,
    NGHTTP2_ENHANCE_YOUR_CALM: 11,
    NGHTTP2_INADEQUATE_SECURITY: 12,
    NGHTTP2_HTTP_1_1_REQUIRED: 13,

    HTTP_STATUS_OK: 200,
    HTTP_STATUS_UNSUPPORTED_MEDIA_TYPE: 415,

    HTTP2_HEADER_STATUS: ':status',
    HTTP2_HEADER_METHOD: ':method',
    HTTP2_HEADER_PATH: ':path',
    HTTP2_HEADER_AUTHORITY: ':authority',
    HTTP2_HEADER_SCHEME: ':scheme',
    HTTP2_HEADER_TE: 'te',
    HTTP2_HEADER_CONTENT_TYPE: 'content-type',
    HTTP2_HEADER_ACCEPT_ENCODING: 'accept-encoding',
    HTTP2_HEADER_USER_AGENT: 'user-agent',
});

function deferHttp2Callback(callback, err = null) {
    setTimeout(() => callback(err), 0);
}

function getDefaultSettings() {
    return {
        headerTableSize: 4096,
        enablePush: 1,
        maxConcurrentStreams: 0xffffffff,
        initialWindowSize: 65535,
        maxFrameSize: 16384,
        maxHeaderListSize: 65536,
    };
}

function ensureHttp2Enabled() {
    if (typeof NativeH2Session !== 'function') {
        throw new Error('HTTP/2 support is not enabled in this xuan build');
    }
}

function toErrorLike(value, fallbackMessage = 'http2 error') {
    if (value instanceof Error) {
        return value;
    }
    const message =
        typeof value === 'string'
            ? value
            : value && typeof value.message === 'string'
              ? value.message
              : fallbackMessage;
    const err = new Error(message);
    if (value && typeof value === 'object') {
        if (value.code !== undefined) {
            err.code = value.code;
        }
        if (value.errno !== undefined) {
            err.errno = value.errno;
        }
        if (value.syscall !== undefined) {
            err.syscall = value.syscall;
        }
    }
    return err;
}

function normalizeArrayBufferLike(chunk) {
    if (typeof Buffer !== 'undefined' && Buffer.isBuffer(chunk)) {
        return new Uint8Array(chunk.buffer, chunk.byteOffset, chunk.byteLength);
    }
    if (chunk instanceof Uint8Array && chunk.constructor === Uint8Array) {
        return chunk;
    }
    if (ArrayBuffer.isView(chunk)) {
        return new Uint8Array(chunk.buffer, chunk.byteOffset, chunk.byteLength);
    }
    if (chunk instanceof ArrayBuffer) {
        return new Uint8Array(chunk);
    }
    return chunk;
}

function toReadableChunk(data) {
    if (typeof Buffer !== 'undefined') {
        if (Buffer.isBuffer(data)) {
            return data;
        }
        if (data instanceof ArrayBuffer) {
            return Buffer.from(data);
        }
        if (ArrayBuffer.isView(data)) {
            return Buffer.from(data.buffer, data.byteOffset, data.byteLength);
        }
    }
    if (data instanceof Uint8Array || data instanceof ArrayBuffer) {
        return data;
    }
    if (ArrayBuffer.isView(data)) {
        return new Uint8Array(data.buffer, data.byteOffset, data.byteLength);
    }
    return data;
}

function normalizeAuthority(authority) {
    let url;
    try {
        url = new URL(authority);
    } catch (err) {
        throw new TypeError(`Invalid authority: ${authority}`);
    }

    const secure = url.protocol === 'https:';
    if (!secure && url.protocol !== 'http:') {
        throw new Error(`Unsupported protocol for http2 client: ${url.protocol}`);
    }

    return {
        authority: url.toString(),
        secure,
        host: url.hostname,
        port: Number(url.port || (secure ? 443 : 80)),
        path: `${url.pathname || '/'}${url.search || ''}`,
    };
}

function attachCallbackProperty(target, native, prop, eventName, mapArgs = (...args) => args, async = false) {
    native[prop] = (...args) => {
        const emit = () => {
            target.emit(eventName, ...mapArgs(...args));
        };
        if (async) {
            queueMicrotask(emit);
            return;
        }
        emit();
    };
}

class ClientHttp2Stream extends Duplex {
    constructor(nativeStream, session) {
        super({ emitClose: false });
        this._native = nativeStream;
        this._session = session;
        this._finalCalled = false;
        this._closeNotified = false;
        this.closed = false;
        this.destroyed = false;
        this._boundNativeResponse = this._handleNativeResponse.bind(this);
        this._boundNativeTrailers = this._handleNativeTrailers.bind(this);
        this._boundNativeData = this._handleNativeData.bind(this);
        this._boundNativeEnd = this._handleNativeEnd.bind(this);
        this._boundNativeError = this._handleNativeError.bind(this);
        this._boundNativeClose = this._handleNativeClose.bind(this);
        this._bindNative();
    }

    _handleNativeResponse(headers, flags, rawHeaders) {
        this.emit('response', headers, flags, rawHeaders);
    }

    _handleNativeTrailers(headers, flags) {
        this.emit('trailers', headers, flags);
    }

    _handleNativeData(chunk) {
        // Only push() to the Readable side; Duplex handles emitting 'data' automatically.
        this.push(toReadableChunk(chunk));
    }

    _handleNativeEnd() {
        // push(null) signals EOF; Duplex will naturally emit 'end' once.
        this.push(null);
        this.resume();
    }

    _handleNativeError(err) {
        this.emit('error', toErrorLike(err));
    }

    _handleNativeClose() {
        if (this.closed) {
            return;
        }
        this.closed = true;
        this.destroyed = this.destroyed || !!this._native.destroyed;
        if (!this._closeNotified) {
            this._closeNotified = true;
            if (typeof this._session?._onStreamClosed === 'function') {
                this._session._onStreamClosed(this);
            }
        }
        this.emit('close');
    }

    _bindNative() {
        this._native.onresponse = this._boundNativeResponse;
        this._native.ontrailers = this._boundNativeTrailers;
        this._native.ondata = this._boundNativeData;
        this._native.onend = this._boundNativeEnd;
        this._native.onerror = this._boundNativeError;
        this._native.onclose = this._boundNativeClose;
    }

    _write(chunk, encoding, callback) {
        try {
            this._native.write(normalizeArrayBufferLike(chunk), encoding);
            deferHttp2Callback(callback, null);
        } catch (err) {
            callback(err);
        }
    }

    _final(callback) {
        if (this._finalCalled) {
            callback();
            return;
        }
        this._finalCalled = true;
        try {
            this._native.end();
            deferHttp2Callback(callback, null);
        } catch (err) {
            callback(err);
        }
    }

    destroy(error) {
        if (this.destroyed) {
            return this;
        }
        this.destroyed = true;
        const err = error !== undefined ? toErrorLike(error) : undefined;
        this._native.destroy(err);
        return this;
    }

    close(code) {
        this._native.close(code);
        return this;
    }

    pause() {
        this._native.pause();
        return super.pause();
    }

    resume() {
        this._native.resume();
        return super.resume();
    }

    get id() {
        return this._native.id;
    }

    get rstCode() {
        return this._native.rstCode;
    }

    get pending() {
        return this._native.pending;
    }

    get session() {
        return this._session;
    }

    get state() {
        return this._native.state;
    }

    get endAfterHeaders() {
        return this._native.endAfterHeaders;
    }

    get sentHeaders() {
        return this._native.sentHeaders;
    }

    get sentTrailers() {
        return this._native.sentTrailers;
    }
}

class ClientHttp2Session extends EventEmitter {
    constructor(nativeSession, socketOverride = null) {
        super();
        this._native = nativeSession;
        this._socketOverride = socketOverride;
        this._socketBridge = null;
        this._activeStreams = new Set();
        this._closeRequested = false;
        this._nativeCloseScheduled = false;
        this._closedOverride = false;
        this._destroyedOverride = false;
        this._closeEventEmitted = false;
        this._bindNative();
    }

    _emitCloseOnce() {
        if (this._closeEventEmitted) {
            return;
        }
        this._closeEventEmitted = true;
        this.emit('close');
    }

    _onStreamClosed(stream) {
        this._activeStreams.delete(stream);
        this._flushPendingClose();
    }

    _flushPendingClose() {
        if (!this._closeRequested || this._nativeCloseScheduled) {
            return;
        }
        if (this._closedOverride || this._destroyedOverride || this._native.closed || this._native.destroyed) {
            return;
        }
        if (this._activeStreams.size > 0) {
            return;
        }

        this._nativeCloseScheduled = true;
        setTimeout(() => {
            this._nativeCloseScheduled = false;
            if (!this._closeRequested) {
                return;
            }
            if (this._closedOverride || this._destroyedOverride || this._native.closed || this._native.destroyed) {
                return;
            }
            this._native.close();
        }, 0);
    }

    _adoptSocketBridge() {
        if (!this._socketOverride || this._socketBridge) {
            return;
        }

        const originalSocket = this._socketOverride;
        const source = this._native.socket ?? originalSocket;
        const bridge = new EventEmitter();

        for (const key of [
            'encrypted',
            'authorized',
            'authorizationError',
            'alpnProtocol',
            'servername',
            'remoteAddress',
            'remotePort',
            'localAddress',
            'localPort',
        ]) {
            if (source && source[key] !== undefined) {
                bridge[key] = source[key];
            }
        }

        for (const method of [
            'getCipher',
            'getPeerCertificate',
            'getCertificate',
            'getProtocol',
        ]) {
            if (source && typeof source[method] === 'function') {
                bridge[method] = source[method].bind(source);
            }
        }

        bridge.closed = false;
        bridge.destroyed = false;

        this._socketBridge = bridge;
        this._socketOverride = bridge;

        queueMicrotask(() => {
            if (originalSocket && originalSocket !== bridge && typeof originalSocket.destroy === 'function') {
                try {
                    originalSocket.destroy();
                } catch {
                    // ignore bridge cleanup errors
                }
            }
        });
    }

    _bindNative() {
        attachCallbackProperty(this, this._native, 'onconnect', 'connect',
            () => [this, this.socket], true);
        this._native.onremotesettings = (settings) => {
            queueMicrotask(() => {
                this._adoptSocketBridge();
                this.emit('remoteSettings', settings);
            });
        };
        attachCallbackProperty(this, this._native, 'onlocalsettings', 'localSettings',
            (settings) => [settings], true);
        attachCallbackProperty(this, this._native, 'ongoaway', 'goaway',
            (errorCode, lastStreamID, opaqueData) => [errorCode, lastStreamID, opaqueData], true);
        attachCallbackProperty(this, this._native, 'onerror', 'error',
            (err) => [toErrorLike(err)], true);
        this._native.onclose = () => {
            this._closedOverride = true;
            const socket = this._socketBridge ?? this._socketOverride;
            if (socket && socket !== this._native.socket) {
                socket.closed = true;
                socket.destroyed = true;
                queueMicrotask(() => {
                    socket.emit?.('close', false);
                });
            }
            queueMicrotask(() => {
                this._emitCloseOnce();
            });
        };
        attachCallbackProperty(this, this._native, 'onping', 'ping',
            (err, duration, payload) => [err, duration, payload], true);
    }

    request(headers = {}, options = undefined) {
        if (this._closeRequested || this._closedOverride || this._native.closed || this._destroyedOverride || this._native.destroyed) {
            throw new Error('The session is closed');
        }

        // Merge session-level TLS options into stream options
        const opts = options ?? {};
        if (this._native._rejectUnauthorized === false) {
            opts.rejectUnauthorized = false;
        }
        const nativeStream = this._native.request(headers, opts);
        const stream = new ClientHttp2Stream(nativeStream, this);
        this._activeStreams.add(stream);
        return stream;
    }

    ping(payload, callback) {
        let cb = callback;
        if (typeof payload === 'function') {
            cb = payload;
        }
        if (typeof cb !== 'function') {
            throw new TypeError('The "callback" argument must be a function');
        }
        try {
            this._native.ping((err, duration, responsePayload) => {
                cb(err ?? null, duration, toReadableChunk(responsePayload));
            });
            return true;
        } catch (err) {
            queueMicrotask(() => cb(toErrorLike(err), undefined, undefined));
            return false;
        }
    }

    setLocalWindowSize(size) {
        return this._native.setLocalWindowSize(size);
    }

    incrementWindowSize(size) {
        return this._native.incrementWindowSize(size);
    }

    ref() {
        this._native.ref();
        return this;
    }

    unref() {
        this._native.unref();
        return this;
    }

    close(callback) {
        if (typeof callback === 'function') {
            this.once('close', callback);
        }
        this._closeRequested = true;
        this._flushPendingClose();
        return this;
    }

    destroy(error) {
        if (this._destroyedOverride || this._native.destroyed) {
            return this;
        }
        this._closeRequested = false;
        this._destroyedOverride = true;
        this._closedOverride = true;
        this._native.destroy(error);
        this._emitCloseOnce();
        return this;
    }

    get closed() {
        return this._closedOverride || this._native.closed;
    }

    get destroyed() {
        return this._destroyedOverride || this._native.destroyed;
    }

    get connecting() {
        return this._native.connecting;
    }

    get encrypted() {
        return this._socketOverride?.encrypted ?? this._native.encrypted;
    }

    get socket() {
        return this._socketOverride ?? this._native.socket ?? null;
    }

    get type() {
        return this._native.type;
    }

    get state() {
        return this._native.state;
    }

    get remoteSettings() {
        return this._native.remoteSettings;
    }

    get localSettings() {
        return this._native.localSettings;
    }

    get pendingSettingsAck() {
        return this._native.pendingSettingsAck;
    }
}

function connect(authority, options = {}, listener) {
    ensureHttp2Enabled();
    const target = normalizeAuthority(authority);
    const opts = options ?? {};

    let socketOverride = null;
    if (typeof opts.createConnection === 'function') {
        socketOverride = opts.createConnection(target.authority, opts) ?? null;
    }

    const nativeSession = new NativeH2Session();
    const session = new ClientHttp2Session(nativeSession, socketOverride);
    
    // Store TLS options for stream creation
    const rejectUnauthorized = opts.rejectUnauthorized !== false;
    nativeSession._rejectUnauthorized = rejectUnauthorized;

    if (typeof listener === 'function') {
        session.once('connect', listener);
    }

    if (opts.settings?.initialWindowSize != null) {
        try {
            nativeSession.setLocalWindowSize(opts.settings.initialWindowSize);
        } catch {
            // Ignore invalid initial settings here and let native connect proceed.
        }
    }

    nativeSession.connect(target.host, target.port, target.path, target.secure);
    return session;
}

function createServer() {
    throw new Error('http2.createServer() is not implemented');
}

function createSecureServer() {
    throw new Error('http2.createSecureServer() is not implemented');
}

export {
    ClientHttp2Session,
    ClientHttp2Stream,
    connect,
    constants,
    createServer,
    createSecureServer,
    getDefaultSettings,
};

export default {
    ClientHttp2Session,
    ClientHttp2Stream,
    connect,
    constants,
    createSecureServer,
    createServer,
    getDefaultSettings,
    sensitiveHeaders: [],
    TLSSocket: tls.TLSSocket,
};
