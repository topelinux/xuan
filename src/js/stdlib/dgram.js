import EventEmitter from 'tjs:events';

const core = globalThis[Symbol.for('tjs.internal.core')];

const textEncoder = new TextEncoder();

function toFamily(ip) {
    return typeof ip === 'string' && ip.includes(':') ? 'IPv6' : 'IPv4';
}

function normalizeTypeArg(type) {
    if (type === 'udp4' || type === 'udp6') {
        return { type };
    }
    if (type && typeof type === 'object') {
        if (type.type !== 'udp4' && type.type !== 'udp6') {
            throw new TypeError('Socket type must be "udp4" or "udp6"');
        }
        return { ...type };
    }
    throw new TypeError('Socket type must be "udp4" or "udp6"');
}

function toUint8Array(message) {
    if (typeof message === 'string') {
        return textEncoder.encode(message);
    }
    if (message instanceof Uint8Array) {
        return message;
    }
    if (message instanceof ArrayBuffer) {
        return new Uint8Array(message);
    }
    if (ArrayBuffer.isView(message)) {
        return new Uint8Array(message.buffer, message.byteOffset, message.byteLength);
    }
    throw new TypeError('The "msg" argument must be of type string, Buffer, ArrayBuffer, or ArrayBufferView');
}

class Socket extends EventEmitter {
    constructor(options) {
        super();
        this._opts = normalizeTypeArg(options);
        this._handle = new core.UDP();
        this._closed = false;
        this._recvStarted = false;
        this._pendingSendCallbacks = [];
        this._connected = false;
        this._bindAddress = undefined;
        this._bindPort = 0;

        this._handle.onrecv = (msg, error) => {
            if (error) {
                this.emit('error', error);
                return;
            }
            const rinfo = {
                address: msg?.addr?.ip ?? '',
                family: toFamily(msg?.addr?.ip),
                port: msg?.addr?.port ?? 0,
                size: msg?.data?.byteLength ?? 0,
            };
            this.emit('message', globalThis.Buffer ? globalThis.Buffer.from(msg.data) : msg.data, rinfo);
        };

        this._handle.onsend = (error) => {
            const cb = this._pendingSendCallbacks.shift();
            if (cb) {
                cb(error || null);
            }
            if (error) {
                this.emit('error', error);
            }
        };
    }

    bind(...args) {
        let port = 0;
        let address = this._opts.type === 'udp6' ? '::' : '0.0.0.0';
        let callback;
        let reuseAddr = !!this._opts.reuseAddr;
        let ipv6Only = !!this._opts.ipv6Only;

        if (typeof args[0] === 'object' && args[0] !== null) {
            const opts = args[0];
            port = opts.port ?? 0;
            address = opts.address ?? address;
            reuseAddr = opts.reuseAddr ?? reuseAddr;
            ipv6Only = opts.ipv6Only ?? ipv6Only;
            if (typeof args[1] === 'function') {
                callback = args[1];
            }
        } else {
            if (typeof args[0] === 'number') {
                port = args[0];
            } else if (typeof args[0] === 'function') {
                callback = args[0];
            }
            if (typeof args[1] === 'string') {
                address = args[1];
            } else if (typeof args[1] === 'function') {
                callback = args[1];
            }
            if (typeof args[2] === 'function') {
                callback = args[2];
            }
        }

        let flags = 0;
        if (reuseAddr) {
            flags |= core.UDP_REUSEADDR;
        }
        if (ipv6Only) {
            flags |= core.UDP_IPV6ONLY;
        }

        this._handle.bind({ ip: address, port }, flags);
        this._handle.startRecv();
        this._recvStarted = true;
        this._bindAddress = address;
        this._bindPort = port;
        queueMicrotask(() => {
            this.emit('listening');
            if (callback) {
                callback();
            }
        });
        return this;
    }

    address() {
        const addr = this._handle.getsockname();
        return {
            address: addr.ip,
            family: toFamily(addr.ip),
            port: addr.port,
        };
    }

    send(message, ...args) {
        let port;
        let address;
        let callback;
        let buffer = toUint8Array(message);

        if (typeof args[0] === 'number' && typeof args[1] === 'number') {
            const offset = args[0];
            const length = args[1];
            port = args[2];
            address = args[3];
            callback = args[4];
            buffer = buffer.subarray(offset, offset + length);
        } else {
            port = args[0];
            address = args[1];
            callback = args[2];
        }

        if (typeof address === 'function') {
            callback = address;
            address = undefined;
        }
        if (typeof port === 'function') {
            callback = port;
            port = undefined;
        }

        let target;
        if (port != null || address != null) {
            if (typeof port !== 'number' || typeof address !== 'string') {
                throw new TypeError('send() requires port number and address string');
            }
            target = { ip: address, port };
        } else if (!this._connected) {
            throw new TypeError('send() requires port and address when socket is not connected');
        }

        const r = this._handle.send(buffer, target);
        if (typeof r === 'number') {
            if (callback) {
                queueMicrotask(() => callback(null, r));
            }
            return r;
        }

        if (callback) {
            this._pendingSendCallbacks.push((err) => callback(err || null, buffer.byteLength));
        } else {
            this._pendingSendCallbacks.push(() => {});
        }
        return undefined;
    }

    close(callback) {
        if (typeof callback === 'function') {
            this.once('close', callback);
        }
        if (this._closed) {
            return this;
        }
        this._closed = true;
        this._handle.onrecv = undefined;
        this._handle.onsend = undefined;
        if (this._recvStarted) {
            this._handle.stopRecv();
            this._recvStarted = false;
        }
        this._pendingSendCallbacks.length = 0;
        this._handle.close();
        queueMicrotask(() => this.emit('close'));
        return this;
    }

    connect(port, address, callback) {
        if (typeof address === 'function') {
            callback = address;
            address = undefined;
        }
        const ip = address ?? (this._opts.type === 'udp6' ? '::1' : '127.0.0.1');
        this._handle.connect({ ip, port });
        this._connected = true;
        queueMicrotask(() => {
            this.emit('connect');
            if (typeof callback === 'function') {
                callback();
            }
        });
        return this;
    }

    remoteAddress() {
        const addr = this._handle.getpeername();
        return {
            address: addr.ip,
            family: toFamily(addr.ip),
            port: addr.port,
        };
    }

    setBroadcast() {
        return this;
    }

    setMulticastLoopback(flag) {
        this._handle.setMulticastLoop(!!flag);
        return this;
    }

    setMulticastTTL(ttl) {
        this._handle.setMulticastTTL(ttl);
        return this;
    }

    setTTL(ttl) {
        this._handle.setMulticastTTL(ttl);
        return this;
    }

    addMembership(multicastAddress, multicastInterface) {
        this._handle.setMembership(multicastAddress, multicastInterface, core.JOIN_GROUP);
        return this;
    }

    dropMembership(multicastAddress, multicastInterface) {
        this._handle.setMembership(multicastAddress, multicastInterface, core.LEAVE_GROUP);
        return this;
    }

    ref() {
        return this;
    }

    unref() {
        return this;
    }
}

function createSocket(type, callback) {
    const socket = new Socket(type);
    if (typeof callback === 'function') {
        socket.on('message', callback);
    }
    return socket;
}

export { Socket, createSocket };
export default { Socket, createSocket };
