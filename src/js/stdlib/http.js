import EventEmitter from 'tjs:events';
import { Readable } from 'tjs:stream';

const core = globalThis[Symbol.for('tjs.internal.core')];
const textEncoder = new TextEncoder();
const EADDRINUSE_ERRNO = typeof core?.UV_EADDRINUSE === 'number' ? core.UV_EADDRINUSE : -1;

const STATUS_CODES = {
    400: 'Bad Request',
    401: 'Unauthorized',
    403: 'Forbidden',
    404: 'Not Found',
    426: 'Upgrade Required',
    500: 'Internal Server Error',
    501: 'Not Implemented',
    503: 'Service Unavailable',
};

function createListenError(code, message, { host = '0.0.0.0', port = 0, errno = EADDRINUSE_ERRNO } = {}) {
    const err = new Error(message);
    err.code = code;
    err.errno = errno;
    err.syscall = 'listen';
    err.address = host;
    err.port = port;
    return err;
}

function isAddrInUseError(err) {
    return err?.code === 'EADDRINUSE' || String(err?.message ?? '').includes('EADDRINUSE');
}

function normalizeListenError(err, { host = '0.0.0.0', port = 0 } = {}) {
    if (isAddrInUseError(err)) {
        return createListenError(
            'EADDRINUSE',
            `listen EADDRINUSE: address already in use ${host}:${port}`,
            { host, port, errno: typeof err?.errno === 'number' ? err.errno : EADDRINUSE_ERRNO },
        );
    }
    return err;
}

function emitErrorAsync(emitter, err) {
    queueMicrotask(() => emitter.emit('error', err));
}

function toUint8Array(chunk, encoding) {
    if (chunk instanceof Uint8Array) return chunk;
    if (chunk instanceof ArrayBuffer) return new Uint8Array(chunk);
    if (ArrayBuffer.isView(chunk)) {
        return new Uint8Array(chunk.buffer, chunk.byteOffset, chunk.byteLength);
    }
    if (typeof chunk === 'string') {
        if (encoding && encoding !== 'utf8' && encoding !== 'utf-8') {
            throw new TypeError(`Unsupported encoding: ${encoding}`);
        }
        return textEncoder.encode(chunk);
    }
    throw new TypeError('chunk must be string, Uint8Array, ArrayBuffer, or ArrayBufferView');
}

function toNodeChunk(chunk) {
    if (typeof Buffer === 'function') {
        return Buffer.from(chunk.buffer, chunk.byteOffset, chunk.byteLength);
    }
    return chunk;
}

function mergeHeaders(target, source) {
    if (!source) return;
    if (source instanceof Headers) {
        source.forEach((value, key) => {
            target[key.toLowerCase()] = String(value);
        });
        return;
    }
    if (Array.isArray(source)) {
        for (let i = 0; i < source.length; i += 2) {
            const key = source[i];
            const value = source[i + 1];
            if (key != null && value != null) {
                target[String(key).toLowerCase()] = String(value);
            }
        }
        return;
    }
    for (const [key, value] of Object.entries(source)) {
        if (value != null) {
            target[key.toLowerCase()] = String(value);
        }
    }
}

function isURL(value) {
    return typeof URL !== 'undefined' && value instanceof URL;
}

function toServerRequestTarget(url) {
    const value = String(url ?? '/');

    try {
        const parsedURL = new URL(value);

        return parsedURL.pathname + parsedURL.search;
    } catch {
        // Node exposes the server-side request target as path + query, not as
        // an absolute URL. Keep relative request targets usable.
    }

    if (value.startsWith('/')) {
        return value;
    }

    if (value.startsWith('?')) {
        return value;
    }

    const schemeIndex = value.indexOf('://');

    if (schemeIndex >= 0) {
        const rest = value.slice(schemeIndex + 3);
        const targetIndex = rest.search(/[/?#]/);

        if (targetIndex >= 0) {
            const target = rest.slice(targetIndex);

            return target || '/';
        }
    }

    return value || '/';
}

function normalizeRequestArgs(defaultProtocol, args) {
    const list = [...args];
    const callback = typeof list[list.length - 1] === 'function' ? list.pop() : undefined;

    let inputURL = undefined;
    let options = undefined;

    if (typeof list[0] === 'string' || isURL(list[0])) {
        inputURL = list.shift();
    }

    if (list.length > 0) {
        options = list.shift();
    }

    if (list.length > 0) {
        throw new TypeError('Too many arguments');
    }

    if (options != null && typeof options !== 'object') {
        throw new TypeError('options must be an object');
    }

    return {
        callback,
        options: options ?? {},
        inputURL,
        defaultProtocol
    };
}

function buildURL({ defaultProtocol, inputURL, options }) {
    if (inputURL != null) {
        const base = isURL(inputURL) ? new URL(inputURL.toString()) : new URL(String(inputURL));
        if (options.path) {
            const p = String(options.path);
            const qmark = p.indexOf('?');
            if (qmark >= 0) {
                base.pathname = p.slice(0, qmark);
                base.search = p.slice(qmark);
            } else {
                base.pathname = p;
                base.search = '';
            }
        }
        if (options.protocol) base.protocol = String(options.protocol);
        if (options.hostname) base.hostname = String(options.hostname);
        if (options.port != null) base.port = String(options.port);
        return base;
    }

    const opts = options ?? {};
    const protocol = String(opts.protocol ?? defaultProtocol);
    const host = opts.hostname ?? opts.host ?? 'localhost';
    const port = opts.port != null ? `:${opts.port}` : '';
    const path = String(opts.path ?? '/');
    return new URL(`${protocol}//${host}${port}${path}`);
}

class IncomingMessage extends Readable {
    constructor(response) {
        super();
        this.statusCode = response.status;
        this.statusMessage = response.statusText || STATUS_CODES[response.status] || '';
        this.httpVersion = '1.1';
        this.url = response.url;
        this.complete = false;

        this.headers = {};
        this.rawHeaders = [];
        response.headers.forEach((value, key) => {
            this.headers[key] = value;
            this.rawHeaders.push(key, value);
        });

        this._readResponseBody(response);
    }

    async _readResponseBody(response) {
        try {
            if (!response.body) {
                this.complete = true;
                this.push(null);
                return;
            }
            const reader = response.body.getReader();
            while (true) {
                const { done, value } = await reader.read();
                if (done) break;
                this.push(toNodeChunk(value));
            }
            this.complete = true;
            this.push(null);
        } catch (err) {
            this.emit('error', err);
            this.push(null);
        }
    }
}

class ServerIncomingMessage extends Readable {
    constructor(request, remoteAddress) {
        super();
        this.method = request.method;
        this.url = toServerRequestTarget(request.url);
        this.httpVersion = '1.1';
        this.complete = false;
        this.socket = { remoteAddress: remoteAddress ?? '' };
        this.headers = {};
        this.rawHeaders = [];
        request.headers.forEach((value, key) => {
            this.headers[key] = value;
            this.rawHeaders.push(key, value);
        });
        this._readRequestBody(request);
    }

    async _readRequestBody(request) {
        try {
            if (!request.body) {
                this.complete = true;
                this.push(null);
                return;
            }
            const reader = request.body.getReader();
            while (true) {
                const { done, value } = await reader.read();
                if (done) break;
                this.push(toNodeChunk(value));
            }
            this.complete = true;
            this.push(null);
        } catch (err) {
            this.emit('error', err);
            this.push(null);
        }
    }
}

function responseFromRawHttp(raw) {
    const text = String(raw ?? '');
    const headerEnd = text.indexOf('\r\n\r\n');
    const head = headerEnd >= 0 ? text.slice(0, headerEnd) : text;
    const body = headerEnd >= 0 ? text.slice(headerEnd + 4) : '';
    const lines = head.split('\r\n');
    const statusLine = lines.shift() ?? '';
    const match = statusLine.match(/^HTTP\/\d(?:\.\d)?\s+(\d{3})(?:\s+(.*))?$/i);
    const status = match ? Number(match[1]) : 500;
    const statusText = match?.[2] ?? STATUS_CODES[status] ?? '';
    const headers = {};

    for (const line of lines) {
        const index = line.indexOf(':');
        if (index <= 0) continue;
        const name = line.slice(0, index).trim().toLowerCase();
        const value = line.slice(index + 1).trim();
        if (name) headers[name] = value;
    }

    return new Response(body, { status, statusText, headers });
}

class UpgradeSocket extends EventEmitter {
    constructor(request, server, remoteAddress) {
        super();
        this.destroyed = false;
        this.remoteAddress = remoteAddress ?? '';
        this._request = request;
        this._server = server;
        this._accepted = false;
        this._written = '';
    }

    write(chunk, encoding, callback) {
        if (typeof encoding === 'function') {
            callback = encoding;
            encoding = undefined;
        }
        if (chunk != null) {
            this._written += typeof chunk === 'string' ? chunk : Buffer.from(chunk).toString(encoding ?? 'utf8');
        }
        if (callback) queueMicrotask(callback);
        return true;
    }

    end(chunk, encoding, callback) {
        if (chunk != null) {
            this.write(chunk, encoding);
        }
        this.destroy();
        if (callback) queueMicrotask(callback);
    }

    destroy(err) {
        this.destroyed = true;
        if (err) this.emit('error', err);
        this.emit('close');
    }
}

function toResponseBody(chunks) {
    if (chunks.length === 0) {
        return null;
    }
    if (chunks.length === 1) {
        return chunks[0];
    }
    let total = 0;
    for (const chunk of chunks) {
        total += chunk.byteLength;
    }
    const merged = new Uint8Array(total);
    let offset = 0;
    for (const chunk of chunks) {
        merged.set(chunk, offset);
        offset += chunk.byteLength;
    }
    return merged;
}

class ServerResponse extends EventEmitter {
    constructor() {
        super();
        this.statusCode = 200;
        this.statusMessage = STATUS_CODES[200] || 'OK';
        this.headersSent = false;
        this.finished = false;
        this._headers = {};
        this._chunks = [];
        this._resolve = null;
        this._promise = new Promise((resolve) => {
            this._resolve = resolve;
        });
    }

    setHeader(name, value) {
        if (this.finished) {
            throw new Error('Cannot set headers after response end');
        }
        this._headers[String(name).toLowerCase()] = String(value);
        return this;
    }

    getHeader(name) {
        return this._headers[String(name).toLowerCase()];
    }

    removeHeader(name) {
        delete this._headers[String(name).toLowerCase()];
        return this;
    }

    getHeaders() {
        return { ...this._headers };
    }

    writeHead(statusCode, statusMessage, headers) {
        if (typeof statusMessage === 'object' && statusMessage !== null) {
            headers = statusMessage;
            statusMessage = undefined;
        }
        this.statusCode = statusCode | 0;
        this.statusMessage = statusMessage != null ? String(statusMessage) : (STATUS_CODES[this.statusCode] || '');
        mergeHeaders(this._headers, headers);
        this.headersSent = true;
        return this;
    }

    write(chunk, encoding, callback) {
        if (typeof encoding === 'function') {
            callback = encoding;
            encoding = undefined;
        }
        if (this.finished) {
            const err = new Error('write after end');
            this.emit('error', err);
            if (callback) callback(err);
            return false;
        }
        try {
            this._chunks.push(toUint8Array(chunk, encoding));
            if (callback) queueMicrotask(() => callback(null));
            return true;
        } catch (err) {
            this.emit('error', err);
            if (callback) callback(err);
            return false;
        }
    }

    end(chunk, encoding, callback) {
        if (typeof chunk === 'function') {
            callback = chunk;
            chunk = undefined;
            encoding = undefined;
        } else if (typeof encoding === 'function') {
            callback = encoding;
            encoding = undefined;
        }

        if (chunk !== undefined) {
            this.write(chunk, encoding);
        }
        if (this.finished) {
            if (callback) queueMicrotask(() => callback());
            return this;
        }

        this.finished = true;
        const body = toResponseBody(this._chunks);
        this._resolve(new Response(body, {
            status: this.statusCode,
            statusText: this.statusMessage,
            headers: this._headers,
        }));
        this.emit('finish');
        if (callback) queueMicrotask(() => callback());
        return this;
    }
}

class Server extends EventEmitter {
    constructor(options, requestListener) {
        super();
        this.listening = false;
        this._host = '0.0.0.0';
        this._port = 0;
        this._server = null;
        this._opts = options ?? {};
        if (typeof requestListener === 'function') {
            this.on('request', requestListener);
        }
    }

    listen(port, host, callback) {
        if (typeof port === 'function') {
            callback = port;
            port = undefined;
            host = undefined;
        } else if (typeof host === 'function') {
            callback = host;
            host = undefined;
        }

        if (this._server) {
            throw new Error('Server is already listening');
        }

        const listenPort = port ?? this._opts.port ?? 0;
        const hasExplicitHost = host !== undefined || this._opts.host !== undefined;
        const requestedHost = hasExplicitHost ? (host ?? this._opts.host) : undefined;
        const makeServer = (listenIp) => {
            const serveOptions = {
                port: listenPort,
                fetch: async (request, { server, remoteAddress }) => {
                    if (request.headers.get('upgrade') === 'websocket' && this.listenerCount('upgrade') > 0) {
                        const req = new ServerIncomingMessage(request, remoteAddress);
                        const socket = new UpgradeSocket(request, server, remoteAddress);
                        const head = typeof Buffer === 'function' ? Buffer.alloc(0) : new Uint8Array(0);
                        this.emit('upgrade', req, socket, head);
                        if (socket._accepted) {
                            return undefined;
                        }
                        if (socket._written) {
                            return responseFromRawHttp(socket._written);
                        }
                        return new Response('Bad Request', { status: 400 });
                    }

                    const req = new ServerIncomingMessage(request, remoteAddress);
                    const res = new ServerResponse();
                    try {
                        if (this.listenerCount('request') === 0) {
                            return new Response('Not Found', { status: 404 });
                        }
                        this.emit('request', req, res);
                        return await res._promise;
                    } catch (err) {
                        this.emit('error', err);
                        return new Response('Internal Server Error', { status: 500 });
                    }
                },
                websocket: {
                    open: (raw) => {
                        raw.data?.wsServer?._handleRawOpen(raw);
                    },
                    message: (raw, data) => {
                        raw.data?.wsServer?._handleRawMessage(raw, data);
                    },
                    error: (raw, error) => {
                        raw.data?.wsServer?._handleRawError(raw, error);
                    },
                    ping: (raw, data) => {
                        raw.data?.wsServer?._handleRawPing(raw, data);
                    },
                    pong: (raw, data) => {
                        raw.data?.wsServer?._handleRawPong(raw, data);
                    },
                    close: (raw, code, reason) => {
                        raw.data?.wsServer?._handleRawClose(raw, code, reason);
                    },
                },
            };
            if (listenIp !== undefined) {
                serveOptions.listenIp = listenIp;
            }
            return tjs.serve(serveOptions);
        };

        // Keep omitted-host behavior stable on current lws backend:
        // avoid "::" fallback because it may report success without a real listener.
        const listenCandidates = hasExplicitHost
            ? [requestedHost]
            : ['0.0.0.0', '127.0.0.1'];

        (async () => {
            let listenError = null;
            for (const candidate of listenCandidates) {
                let server;
                try {
                    server = makeServer(candidate);
                } catch (err) {
                    listenError = err;
                    continue;
                }

                const effectiveHost = candidate ?? '0.0.0.0';
                if (listenPort > 0 && server.port !== listenPort) {
                    const err = createListenError(
                        'EADDRINUSE',
                        `listen EADDRINUSE: address already in use ${effectiveHost}:${listenPort}`,
                        { host: effectiveHost, port: listenPort },
                    );
                    err.boundPort = server.port;
                    try {
                        server.close();
                    } catch {
                        // ignore
                    }
                    listenError = err;
                    continue;
                }

                this._server = server;
                this._host = effectiveHost;
                this._port = server.port;
                this.listening = true;
                this.emit('listening');
                if (typeof callback === 'function') {
                    callback();
                }
                return;
            }

            let err = listenError ?? new Error('failed to listen');
            if (listenPort > 0 && isAddrInUseError(err)) {
                err = normalizeListenError(err, {
                    host: requestedHost ?? '0.0.0.0',
                    port: listenPort,
                });
            } else if (listenPort > 0 && !(err && err.code)) {
                err = createListenError(
                    'EADDRINUSE',
                    `listen EADDRINUSE: address already in use ${requestedHost ?? '0.0.0.0'}:${listenPort}`,
                    { host: requestedHost ?? '0.0.0.0', port: listenPort },
                );
            }
            this._server = null;
            this._host = '0.0.0.0';
            this._port = 0;
            this.listening = false;
            emitErrorAsync(this, err);
        })();
        return this;
    }

    address() {
        if (!this._server) {
            return null;
        }
        return {
            address: this._host,
            family: this._host.includes(':') ? 'IPv6' : 'IPv4',
            port: this._port,
        };
    }

    close(callback) {
        if (typeof callback === 'function') {
            this.once('close', callback);
        }
        if (!this._server) {
            queueMicrotask(() => this.emit('close'));
            return this;
        }
        this._server.close(() => {
            this._server = null;
            this.listening = false;
            this.emit('close');
        });
        return this;
    }
}

function createServer(options, requestListener) {
    if (typeof options === 'function') {
        requestListener = options;
        options = undefined;
    }
    return new Server(options, requestListener);
}

class ClientRequest extends EventEmitter {
    constructor(defaultProtocol, inputURL, options, callback) {
        super();
        this.aborted = false;
        this.destroyed = false;
        this.finished = false;
        this.method = String(options.method ?? 'GET').toUpperCase();
        this.path = String(options.path ?? '/');
        this._chunks = [];
        this._headers = {};
        this._controller = new AbortController();
        this._timeout = null;
        this._callback = callback;
        this._url = buildURL({ defaultProtocol, inputURL, options });
        this._rejectUnauthorized = options.rejectUnauthorized;

        mergeHeaders(this._headers, options.headers);
        if (options.auth && this._headers.authorization == null) {
            if (typeof globalThis.btoa !== 'function') {
                throw new Error('options.auth requires btoa support');
            }
            const encoded = globalThis.btoa(String(options.auth));
            this._headers.authorization = `Basic ${encoded}`;
        }
    }

    setHeader(name, value) {
        this._headers[String(name).toLowerCase()] = String(value);
        return this;
    }

    getHeader(name) {
        return this._headers[String(name).toLowerCase()];
    }

    removeHeader(name) {
        delete this._headers[String(name).toLowerCase()];
        return this;
    }

    getHeaders() {
        return { ...this._headers };
    }

    write(chunk, encoding, callback) {
        if (typeof encoding === 'function') {
            callback = encoding;
            encoding = undefined;
        }
        if (this.finished || this.destroyed) {
            const err = new Error('write after end');
            this.emit('error', err);
            if (callback) callback(err);
            return false;
        }
        try {
            this._chunks.push(toUint8Array(chunk, encoding));
            if (callback) queueMicrotask(() => callback(null));
            return true;
        } catch (err) {
            this.emit('error', err);
            if (callback) callback(err);
            return false;
        }
    }

    async _dispatch() {
        let body = undefined;
        if (this._chunks.length === 1) {
            body = this._chunks[0];
        } else if (this._chunks.length > 1) {
            let total = 0;
            for (const chunk of this._chunks) total += chunk.byteLength;
            const merged = new Uint8Array(total);
            let offset = 0;
            for (const chunk of this._chunks) {
                merged.set(chunk, offset);
                offset += chunk.byteLength;
            }
            body = merged;
        }

        try {
            const fetchOptions = {
                method: this.method,
                headers: this._headers,
                body,
                signal: this._controller.signal,
            };
            
            // Pass TLS options to fetch
            if (this._rejectUnauthorized !== undefined) {
                fetchOptions.rejectUnauthorized = this._rejectUnauthorized;
            }
            
            const response = await fetch(this._url, fetchOptions);

            if (this.destroyed) return;
            const incoming = new IncomingMessage(response);
            this.emit('response', incoming);
            if (this._callback) this._callback(incoming);
            this.emit('finish');
        } catch (err) {
            if (this.destroyed) return;
            this.emit('error', err);
        } finally {
            if (this._timeout != null) clearTimeout(this._timeout);
        }
    }

    end(chunk, encoding, callback) {
        if (typeof chunk === 'function') {
            callback = chunk;
            chunk = undefined;
            encoding = undefined;
        } else if (typeof encoding === 'function') {
            callback = encoding;
            encoding = undefined;
        }

        if (chunk !== undefined) {
            this.write(chunk, encoding);
        }

        if (this.finished || this.destroyed) {
            if (callback) queueMicrotask(() => callback());
            return this;
        }

        this.finished = true;
        if (callback) this.once('finish', callback);
        queueMicrotask(() => this._dispatch());
        return this;
    }

    abort() {
        return this.destroy();
    }

    destroy(err) {
        if (this.destroyed) return this;
        this.destroyed = true;
        this.aborted = true;
        this._controller.abort();
        if (this._timeout != null) clearTimeout(this._timeout);
        if (err) this.emit('error', err);
        this.emit('close');
        return this;
    }

    setTimeout(ms, callback) {
        if (typeof callback === 'function') {
            this.once('timeout', callback);
        }
        if (this._timeout != null) clearTimeout(this._timeout);
        if (ms > 0) {
            this._timeout = setTimeout(() => {
                this.emit('timeout');
            }, ms);
        } else {
            this._timeout = null;
        }
        return this;
    }

    flushHeaders() {
        return this;
    }
}

function request(...args) {
    const { defaultProtocol, inputURL, options, callback } = normalizeRequestArgs('http:', args);
    return new ClientRequest(defaultProtocol, inputURL, options, callback);
}

function get(...args) {
    const req = request(...args);
    req.end();
    return req;
}

export {
    STATUS_CODES,
    createServer,
    Server,
    ServerResponse,
    ClientRequest,
    IncomingMessage,
    request,
    get,
};

export default {
    STATUS_CODES,
    createServer,
    Server,
    ServerResponse,
    ClientRequest,
    IncomingMessage,
    request,
    get,
};
