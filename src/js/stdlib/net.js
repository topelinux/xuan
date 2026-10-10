import Stream from 'tjs:stream';
import EventEmitter from 'tjs:events';

const { Duplex } = Stream;
const core = globalThis[Symbol.for('tjs.internal.core')];
const EADDRINUSE_ERRNO = typeof core?.UV_EADDRINUSE === 'number' ? core.UV_EADDRINUSE : -1;

function toNativeWriteChunk(chunk) {
    if (chunk instanceof Uint8Array && chunk.constructor === Uint8Array) {
        return chunk;
    }

    if (chunk instanceof Uint8Array) {
        return new Uint8Array(chunk.buffer, chunk.byteOffset, chunk.byteLength);
    }

    if (ArrayBuffer.isView(chunk)) {
        return new Uint8Array(chunk.buffer, chunk.byteOffset, chunk.byteLength);
    }

    if (chunk instanceof ArrayBuffer) {
        return new Uint8Array(chunk);
    }

    return chunk;
}

function isIP(host) {
    if (typeof host !== 'string') return 0;
    if (/^(?:\d{1,3}\.){3}\d{1,3}$/.test(host)) return 4;
    if (/^[0-9a-fA-F:]+$/.test(host)) return 6;
    return 0;
}

function defineErrorProperty(err, key, value) {
    if (value === undefined) return;
    Object.defineProperty(err, key, {
        value,
        writable: true,
        configurable: true,
        enumerable: true,
    });
}

function isAddrInUseError(err) {
    return err?.code === 'EADDRINUSE' || String(err?.message ?? '').includes('EADDRINUSE');
}

function normalizeListenError(err, { host = '0.0.0.0', port = 0 } = {}) {
    if (!isAddrInUseError(err)) {
        return err;
    }

    const out = err instanceof Error ? err : new Error(String(err));
    out.message = `listen EADDRINUSE: address already in use ${host}:${port}`;
    defineErrorProperty(out, 'code', 'EADDRINUSE');
    defineErrorProperty(out, 'errno', typeof err?.errno === 'number' ? err.errno : EADDRINUSE_ERRNO);
    defineErrorProperty(out, 'syscall', 'listen');
    defineErrorProperty(out, 'address', host);
    defineErrorProperty(out, 'port', port);
    return out;
}

function emitErrorAsync(emitter, err) {
    queueMicrotask(() => emitter.emit('error', err));
}

async function resolveAddress(host, port) {
    if (isIP(host)) {
        return { ip: host, port };
    }
    const res = await core.getaddrinfo(host, { family: core.AF_UNSPEC });
    if (!res || !res.length) {
        throw new Error(`DNS lookup failed for ${host}`);
    }
    return { ip: res[0].ip, port };
}

class Socket extends Duplex {
    constructor(options = {}) {
        const socketOptions = { ...options };
        const providedHandle = socketOptions._handle;
        const HandleCtor = socketOptions._handleCtor ?? core.TCP;
        const handleConfig = socketOptions._handleConfig;
        const initiallyConnected = socketOptions._connected === true;
        delete socketOptions._handleCtor;
        delete socketOptions._handleConfig;
        delete socketOptions._handle;
        delete socketOptions._connected;

        super(socketOptions);
        this._handle = providedHandle ?? new HandleCtor();
        if (handleConfig && typeof this._handle.configure === 'function') {
            this._handle.configure(handleConfig);
        }
        this.connecting = false;
        this._connected = initiallyConnected;
        this.destroyed = false;
        this._pendingWrites = [];
        this._preconnectWrites = [];
        this._timeoutMs = 0;
        this._timeoutTimer = null;
        this.remoteAddress = undefined;
        this.remotePort = undefined;
        this.localAddress = undefined;
        this.localPort = undefined;
        this._setupHandle();
        if (this._connected) {
            this._updateAddresses();
            this._handle.startRead();
        }
    }

    _setupHandle() {
        this._handle.onread = (data, error) => {
            if (error) {
                this._clearTimeoutTimer();
                this.emit('error', error);
                this.destroy();
                return;
            }
            if (data === null) {
                this._clearTimeoutTimer();
                this.push(null);
                this.emit('close');
                return;
            }
            this._refreshTimeout();
            // Node net sockets emit Buffer chunks by default.
            if (typeof Buffer !== 'undefined' && !Buffer.isBuffer(data)) {
                if (data instanceof ArrayBuffer) {
                    this.push(Buffer.from(data));
                    return;
                }
                if (ArrayBuffer.isView(data)) {
                    this.push(Buffer.from(data.buffer, data.byteOffset, data.byteLength));
                    return;
                }
            }
            this.push(data);
        };

        this._handle.onwrite = (error) => {
            const pending = this._pendingWrites.shift();
            if (!pending) return;
            if (error) {
                pending.cb(error);
                this.emit('error', error);
            } else {
                pending.cb(null);
                this._refreshTimeout();
                this.emit('drain');
            }
        };

        this._handle.onconnect = (error) => {
            this.connecting = false;
            if (error) {
                while (this._preconnectWrites.length > 0) {
                    this._preconnectWrites.shift().cb(error);
                }
                this.emit('error', error);
                return;
            }
            this._connected = true;
            this._updateAddresses();
            this._handle.startRead();
            this._flushPreconnectWrites();
            this._refreshTimeout();
            this.emit('connect');
        };
    }

    _updateAddresses() {
        try {
            const peer = this._handle.getpeername();
            if (peer) {
                this.remoteAddress = peer.ip;
                this.remotePort = peer.port;
            }
        } catch {
            // ignore
        }
        try {
            const local = this._handle.getsockname();
            if (local) {
                this.localAddress = local.ip;
                this.localPort = local.port;
            }
        } catch {
            // ignore
        }
    }

    _clearTimeoutTimer() {
        if (!this._timeoutTimer) return;
        clearTimeout(this._timeoutTimer);
        this._timeoutTimer = null;
    }

    _refreshTimeout() {
        if (!this._timeoutMs || this.destroyed) return;
        this._clearTimeoutTimer();
        this._timeoutTimer = setTimeout(() => {
            this._timeoutTimer = null;
            this.emit('timeout');
        }, this._timeoutMs);
    }

    _writeToHandle(chunk, callback) {
        try {
            const result = this._handle.write(toNativeWriteChunk(chunk));
            if (typeof result === 'number') {
                callback(null);
            } else {
                this._pendingWrites.push({ cb: callback });
            }
        } catch (err) {
            callback(err);
        }
    }

    _flushPreconnectWrites() {
        if (!this._connected || this.destroyed) return;
        while (this._preconnectWrites.length > 0) {
            const { chunk, cb } = this._preconnectWrites.shift();
            this._writeToHandle(chunk, cb);
        }
    }

    async connect(port, host, connectListener) {
        if (typeof host === 'function') {
            connectListener = host;
            host = undefined;
        }
        if (typeof port === 'object' && port) {
            host = port.host ?? port.hostname;
            port = port.port;
        }
        const targetHost = host ?? 'localhost';
        const targetPort = Number(port ?? 0);
        if (connectListener) this.once('connect', connectListener);
        this.connecting = true;
        try {
            const addr = await resolveAddress(targetHost, targetPort);
            this._handle.connect(addr);
        } catch (error) {
            this.connecting = false;
            queueMicrotask(() => this.emit('error', error));
        }
        return this;
    }

    _write(chunk, _encoding, callback) {
        if (this.destroyed) {
            callback(new Error('socket destroyed'));
            return;
        }
        if (!this._connected) {
            this._preconnectWrites.push({ chunk, cb: callback });
            return;
        }
        this._writeToHandle(chunk, callback);
    }

    end(chunk, encoding, callback) {
        if (chunk !== undefined) {
            this.write(chunk, encoding);
        }
        try {
            this._handle.shutdown();
        } catch {
            // ignore
        }
        return super.end(undefined, undefined, callback);
    }

    destroy(err) {
        if (this.destroyed) return this;
        this.destroyed = true;
        this._clearTimeoutTimer();
        while (this._preconnectWrites.length > 0) {
            this._preconnectWrites.shift().cb(err ?? new Error('socket destroyed'));
        }
        try {
            this._handle.close();
        } catch {
            // ignore
        }
        if (err) this.emit('error', err);
        this.emit('close');
        return this;
    }

    setNoDelay(enable = true) {
        try {
            this._handle.setNoDelay(!!enable);
        } catch {
            // ignore
        }
        return this;
    }

    setKeepAlive(enable = true, delay = 0) {
        try {
            this._handle.setKeepAlive(!!enable, delay);
        } catch {
            // ignore
        }
        return this;
    }

    setTimeout(timeout = 0, callback) {
        if (typeof callback === 'function') {
            this.once('timeout', callback);
        }
        const ms = Number(timeout);
        this._timeoutMs = Number.isFinite(ms) && ms > 0 ? Math.trunc(ms) : 0;
        if (!this._timeoutMs) {
            this._clearTimeoutTimer();
        } else {
            this._refreshTimeout();
        }
        return this;
    }

    getPeerCertificate() {
        return {};
    }
}

class Server extends EventEmitter {
    constructor(options, connectionListener) {
        super();
        if (typeof options === 'function') {
            connectionListener = options;
            options = undefined;
        }
        this._options = options ?? {};
        this._handle = null;
        this._address = null;
        this.listening = false;
        this.connections = 0;
        if (typeof connectionListener === 'function') {
            this.on('connection', connectionListener);
        }
    }

    listen(...args) {
        let callback;
        if (typeof args[args.length - 1] === 'function') {
            callback = args.pop();
        }
        if (callback) {
            this.once('listening', callback);
        }

        let options = {};
        if (args.length > 0 && typeof args[0] === 'object' && args[0] !== null) {
            options = { ...args[0] };
        } else {
            options.port = args[0];
            options.host = args[1];
            options.backlog = args[2];
        }
        options = { ...this._options, ...options };

        const port = Number(options.port ?? 0);
        const host = options.host ?? '0.0.0.0';
        const backlog = Number(options.backlog ?? 511);
        const ipv6Only = !!options.ipv6Only;

        if (this.listening || this._handle) {
            queueMicrotask(() => this.emit('error', new Error('Server is already listening')));
            return this;
        }

        const handle = new core.TCP();
        this._handle = handle;

        handle.onconnection = (error, clientHandle) => {
            if (typeof error === 'undefined' && typeof clientHandle === 'undefined') {
                return;
            }
            if (error) {
                this.emit('error', error);
                return;
            }

            const socket = new Socket({
                _handle: clientHandle,
                _connected: true,
            });
            this.connections += 1;
            socket.once('close', () => {
                this.connections = Math.max(0, this.connections - 1);
            });
            this.emit('connection', socket);
        };

        (async () => {
            try {
                const addr = await resolveAddress(host, port);
                let flags = 0;
                if (ipv6Only) {
                    flags |= core.TCP_IPV6ONLY;
                }
                handle.bind(addr, flags);
                handle.listen(backlog);
                const local = handle.getsockname();
                this._address = {
                    address: local.ip,
                    family: isIP(local.ip) === 6 ? 'IPv6' : 'IPv4',
                    port: local.port,
                };
                this.listening = true;
                this.emit('listening');
            } catch (error) {
                this._handle = null;
                try {
                    handle.close();
                } catch {
                    // ignore
                }
                emitErrorAsync(this, normalizeListenError(error, { host, port }));
            }
        })();

        return this;
    }

    address() {
        return this._address;
    }

    close(callback) {
        if (typeof callback === 'function') {
            this.once('close', callback);
        }
        const handle = this._handle;
        this._handle = null;
        this.listening = false;
        if (!handle) {
            queueMicrotask(() => this.emit('close'));
            return this;
        }
        handle.onconnection = undefined;
        try {
            handle.close();
        } catch {
            // ignore
        }
        queueMicrotask(() => this.emit('close'));
        return this;
    }

    getConnections(callback) {
        if (typeof callback === 'function') {
            queueMicrotask(() => callback(null, this.connections));
        }
    }
}

function createServer(options, connectionListener) {
    return new Server(options, connectionListener);
}

function createConnection(port, host, connectListener) {
    const socket = new Socket();
    socket.connect(port, host, connectListener).catch(error => {
        queueMicrotask(() => socket.emit('error', error));
    });
    return socket;
}

const connect = createConnection;

export {
    Socket,
    Server,
    createServer,
    createConnection,
    connect,
    isIP,
    isIPv4,
    isIPv6,
};

function isIPv4(input) {
    return isIP(input) === 4;
}

function isIPv6(input) {
    return isIP(input) === 6;
}

export default {
    Socket,
    Server,
    createServer,
    createConnection,
    connect,
    isIP,
    isIPv4,
    isIPv6,
};
