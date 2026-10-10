import EventEmitter from 'tjs:events';
import { Buffer } from 'tjs:buffer';

const kRaw = Symbol('kRawWebSocket');
const kBinaryType = Symbol('kBinaryType');
const kServer = Symbol('kServer');

function toNodeRequest(request, remoteAddress) {
    let url = '/';
    let headers = {};

    try {
        const parsed = new URL(request.url);
        url = `${parsed.pathname}${parsed.search}`;
        headers = Object.fromEntries(request.headers.entries());
    } catch {
        // Keep defaults.
    }

    return {
        url,
        headers,
        socket: { remoteAddress },
    };
}

function toBinaryPayload(data, binaryType) {
    if (data instanceof ArrayBuffer) {
        if (binaryType === 'arraybuffer') {
            return data.slice(0);
        }
        return Buffer.from(data);
    }

    if (ArrayBuffer.isView(data)) {
        if (binaryType === 'arraybuffer') {
            return data.buffer.slice(data.byteOffset, data.byteOffset + data.byteLength);
        }
        return Buffer.from(data.buffer, data.byteOffset, data.byteLength);
    }

    return Buffer.from(String(data));
}

function toControlPayload(data) {
    if (data == null) {
        return new ArrayBuffer(0);
    }

    let payload;
    if (typeof data === 'string') {
        payload = Buffer.from(data);
    } else if (data instanceof ArrayBuffer) {
        payload = data;
    } else if (ArrayBuffer.isView(data)) {
        payload = data.buffer.slice(data.byteOffset, data.byteOffset + data.byteLength);
    } else {
        payload = Buffer.from(String(data));
    }

    const length = payload.byteLength ?? payload.length;
    if (length > 125) {
        throw new RangeError('WebSocket control frame payload too large');
    }
    if (payload instanceof ArrayBuffer) {
        return payload;
    }
    return payload.buffer.slice(payload.byteOffset, payload.byteOffset + payload.byteLength);
}

function sendRaw(raw, data) {
    if (typeof raw.send === 'function' && typeof raw.sendText !== 'function') {
        raw.send(data);
        return;
    }

    if (typeof data === 'string') {
        raw.sendText(data);
        return;
    }

    if (data instanceof ArrayBuffer) {
        raw.sendBinary(data, 0, data.byteLength);
        return;
    }

    if (ArrayBuffer.isView(data)) {
        raw.sendBinary(data.buffer, data.byteOffset, data.byteLength);
        return;
    }

    if (data instanceof Blob) {
        data.arrayBuffer().then((buf) => {
            raw.sendBinary(buf, 0, buf.byteLength);
        });
        return;
    }

    raw.sendText(String(data));
}

function normalizeProtocols(protocols) {
    if (protocols == null) {
        return [];
    }
    if (typeof protocols === 'string') {
        return [ protocols ];
    }
    return Array.from(protocols);
}

function normalizeHeaders(headers) {
    if (headers == null) {
        return null;
    }

    if (typeof headers.forEach === 'function') {
        const result = [];
        headers.forEach((value, name) => result.push([ name, value ]));
        return result;
    }

    if (Array.isArray(headers)) {
        return headers.map(([ name, value ]) => [ String(name), String(value) ]);
    }

    return Object.keys(headers).map((name) => [ name, String(headers[name]) ]);
}

class WSConnection extends EventEmitter {
    static CONNECTING = 0;
    static OPEN = 1;
    static CLOSING = 2;
    static CLOSED = 3;
    CONNECTING = 0;
    OPEN = 1;
    CLOSING = 2;
    CLOSED = 3;

    constructor(raw, server) {
        super();
        this[kRaw] = raw;
        this[kServer] = server;
        this[kBinaryType] = 'nodebuffer';
        this.readyState = WSConnection.OPEN;
        this.protocol = '';
    }

    get bufferedAmount() {
        return this[kRaw]?.bufferedAmount ?? 0;
    }

    get binaryType() {
        return this[kBinaryType];
    }

    set binaryType(value) {
        if ([ 'nodebuffer', 'arraybuffer', 'fragments' ].includes(value)) {
            this[kBinaryType] = value;
        }
    }

    send(data, options, cb) {
        if (typeof options === 'function') {
            cb = options;
        }

        if (this.readyState !== WSConnection.OPEN) {
            const err = new Error('WebSocket is not open');
            if (cb) cb(err);
            return;
        }

        try {
            sendRaw(this[kRaw], data);
            if (cb) cb();
        } catch (err) {
            if (cb) cb(err);
            else this.emit('error', err);
        }
    }

    close(code, reason) {
        if (this.readyState === WSConnection.CLOSING || this.readyState === WSConnection.CLOSED) {
            return;
        }
        this.readyState = WSConnection.CLOSING;
        this[kRaw].close(code, reason);
    }

    terminate() {
        this.close(1001, 'terminated');
    }

    ping(data, mask, cb) {
        if (typeof mask === 'function') cb = mask;
        if (typeof data === 'function') {
            cb = data;
            data = undefined;
        }

        if (this.readyState !== WSConnection.OPEN || typeof this[kRaw]?.ping !== 'function') {
            const err = new Error('WebSocket is not open');
            if (cb) cb(err);
            else this.emit('error', err);
            return;
        }

        try {
            this[kRaw].ping(toControlPayload(data));
            if (cb) cb();
        } catch (err) {
            if (cb) cb(err);
            else this.emit('error', err);
        }
    }

    pong(data, mask, cb) {
        if (typeof mask === 'function') cb = mask;
        if (typeof data === 'function') {
            cb = data;
            data = undefined;
        }

        if (this.readyState !== WSConnection.OPEN || typeof this[kRaw]?.pong !== 'function') {
            const err = new Error('WebSocket is not open');
            if (cb) cb(err);
            else this.emit('error', err);
            return;
        }

        try {
            this[kRaw].pong(toControlPayload(data));
            if (cb) cb();
        } catch (err) {
            if (cb) cb(err);
            else this.emit('error', err);
        }
    }
}

class WebSocketServer extends EventEmitter {
    constructor(options = {}, callback) {
        super();

        if (typeof callback === 'function') {
            this.on('connection', callback);
        }

        const {
            host = '0.0.0.0',
            port = 0,
            path,
            clientTracking = true,
            noServer = false,
        } = options;

        if (options.server) {
            throw new Error('server mode is not supported yet');
        }

        this.clients = new Set();
        this._trackClients = clientTracking !== false;
        this._connections = new Map();
        this._path = path || null;
        this._host = host;
        this._noServer = noServer;
        this._server = null;

        if (noServer) {
            return;
        }

        this._server = tjs.serve({
            port,
            listenIp: host,
            fetch: (request, { server, remoteAddress }) => {
                if (this._path) {
                    const pathname = new URL(request.url).pathname;
                    if (pathname !== this._path) {
                        return new Response('Not Found', { status: 404 });
                    }
                }

                if (request.headers.get('upgrade') !== 'websocket') {
                    return new Response('Upgrade Required', { status: 426 });
                }

                const data = {
                    request: toNodeRequest(request, remoteAddress),
                    wsServer: this,
                };
                const ok = server.upgrade(request, { data });
                return ok ? undefined : new Response('Bad Request', { status: 400 });
            },
            websocket: {
                open: (raw) => {
                    this._handleRawOpen(raw);
                },
                message: (raw, data) => {
                    this._handleRawMessage(raw, data);
                },
                error: (raw, error) => {
                    this._handleRawError(raw, error);
                },
                ping: (raw, data) => {
                    this._handleRawPing(raw, data);
                },
                pong: (raw, data) => {
                    this._handleRawPong(raw, data);
                },
                close: (raw, code, reason) => {
                    this._handleRawClose(raw, code, reason);
                },
            },
        });

        queueMicrotask(() => {
            this.emit('listening');
        });
    }

    handleUpgrade(req, socket, head, cb) {
        if (!this._noServer) {
            throw new Error('handleUpgrade is only available in noServer mode');
        }
        if (!socket?._server || !socket?._request) {
            throw new Error('invalid upgrade socket');
        }
        const data = {
            request: {
                url: req?.url ?? '/',
                headers: req?.headers ?? {},
                socket: req?.socket ?? { remoteAddress: socket.remoteAddress ?? '' },
            },
            wsServer: this,
            wsCallback: typeof cb === 'function' ? cb : null,
        };
        const ok = socket._server.upgrade(socket._request, { data });
        socket._accepted = !!ok;
        return ok;
    }

    _handleRawOpen(raw) {
        const connection = new WSConnection(raw, this);
        this._connections.set(raw, connection);
        if (this._trackClients) {
            this.clients.add(connection);
        }
        const req = raw.data?.request ?? { url: '/', headers: {}, socket: { remoteAddress: '' } };
        const cb = raw.data?.wsCallback;
        if (typeof cb === 'function') {
            cb(connection, req);
        } else {
            this.emit('connection', connection, req);
        }
    }

    _handleRawMessage(raw, data) {
        const connection = this._connections.get(raw);
        if (!connection) return;
        if (typeof data === 'string') {
            connection.emit('message', data, false);
            return;
        }
        connection.emit('message', toBinaryPayload(data, connection.binaryType), true);
    }

    _handleRawError(raw, error) {
        const connection = this._connections.get(raw);
        const err = error instanceof Error ? error : new Error(String(error ?? 'WebSocket error'));
        if (connection) {
            connection.emit('error', err);
        } else {
            this.emit('error', err);
        }
    }

    _handleRawPong(raw, data) {
        const connection = this._connections.get(raw);
        if (!connection) return;
        connection.emit('pong', Buffer.from(data));
    }

    _handleRawPing(raw, data) {
        const connection = this._connections.get(raw);
        if (!connection) return;
        connection.emit('ping', Buffer.from(data));
    }

    _handleRawClose(raw, code, reason) {
        const connection = this._connections.get(raw);
        if (!connection) return;
        connection.readyState = WSConnection.CLOSED;
        this._connections.delete(raw);
        this.clients.delete(connection);
        connection.emit('close', code, Buffer.from(reason ?? ''));
    }

    address() {
        if (!this._server) {
            return null;
        }
        return {
            address: this._host,
            family: this._host.includes(':') ? 'IPv6' : 'IPv4',
            port: this._server.port,
        };
    }

    close(cb) {
        if (typeof cb === 'function') {
            this.once('close', cb);
        }
        if (this._server) {
            this._server.close();
            this._server = null;
        }
        this.emit('close');
    }
}

class NodeWebSocket extends WSConnection {
    constructor(address, protocols, options) {
        if (protocols && typeof protocols === 'object' && !Array.isArray(protocols)) {
            options = protocols;
            protocols = undefined;
        }

        const raw = new globalThis.WebSocket(address, normalizeProtocols(protocols), normalizeHeaders(options?.headers));
        super(raw, null);
        this.readyState = WSConnection.CONNECTING;
        this.url = String(address);

        raw.onopen = () => {
            setTimeout(() => {
                this.readyState = WSConnection.OPEN;
                this.emit('open');
            }, 0);
        };
        raw.onmessage = (event) => {
            setTimeout(() => {
                const payload = typeof event.data === 'string' ? event.data : toBinaryPayload(event.data, this.binaryType);
                this.emit('message', payload, typeof payload !== 'string');
            }, 0);
        };
        raw.onerror = (reason) => {
            const message = reason?.message || reason?.error?.message || reason || 'WebSocket error';
            setTimeout(() => {
                this.emit('error', reason instanceof Error ? reason : new Error(String(message)));
            }, 0);
        };
        raw.onpong = (data) => {
            setTimeout(() => {
                this.emit('pong', Buffer.from(data));
            }, 0);
        };
        raw.onping = (data) => {
            setTimeout(() => {
                this.emit('ping', Buffer.from(data));
            }, 0);
        };
        raw.onclose = (event) => {
            setTimeout(() => {
                this.readyState = WSConnection.CLOSED;
                this.emit('close', event.code, Buffer.from(event.reason ?? ''));
            }, 0);
        };
    }
}

NodeWebSocket.Server = WebSocketServer;
NodeWebSocket.WebSocketServer = WebSocketServer;
NodeWebSocket.CONNECTING = WSConnection.CONNECTING;
NodeWebSocket.OPEN = WSConnection.OPEN;
NodeWebSocket.CLOSING = WSConnection.CLOSING;
NodeWebSocket.CLOSED = WSConnection.CLOSED;

export { NodeWebSocket as WebSocket, WebSocketServer, WebSocketServer as Server };
export default NodeWebSocket;
