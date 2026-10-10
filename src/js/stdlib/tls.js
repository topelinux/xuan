import net from 'tjs:net';
const core = globalThis[Symbol.for('tjs.internal.core')];

class TLSSocket extends net.Socket {
    constructor(options = {}) {
        const host = options.host ?? options.hostname;
        const tlsOptions = {
            servername: options.servername ?? (typeof host === 'string' ? host : undefined),
            rejectUnauthorized: options.rejectUnauthorized !== false,
            ca: options.ca,
        };
        super({
            ...options,
            _handleCtor: core.TLS,
            _handleConfig: tlsOptions,
        });
        this.encrypted = true;
        this.authorized = tlsOptions.rejectUnauthorized ? true : false;
        this.authorizationError = null;
        this.secureConnecting = false;
        this.servername = tlsOptions.servername;
        this.alpnProtocol = null;
    }

    getCipher() {
        return {
            name: null,
            standardName: null,
            version: null,
        };
    }

    getProtocol() {
        return null;
    }

    getCertificate() {
        return {};
    }

    getPeerCertificate() {
        return {};
    }

    setServername(servername) {
        this.servername = servername;
        return this;
    }
}

function normalizeConnectArgs(args) {
    const list = [ ...args ];
    let callback = typeof list[list.length - 1] === 'function' ? list.pop() : undefined;

    let options = {};
    if (typeof list[0] === 'object' && list[0] !== null) {
        options = { ...list[0] };
    } else {
        const [ port, host, opts ] = list;
        options = { ...(opts ?? {}) };
        if (typeof port !== 'undefined') {
            options.port = port;
        }
        if (typeof host === 'string') {
            options.host = host;
        }
    }

    return { options, callback };
}

function connect(...args) {
    const { options, callback } = normalizeConnectArgs(args);
    const socket = new TLSSocket(options);
    socket.secureConnecting = true;

    socket.once('connect', () => {
        socket.secureConnecting = false;
        socket.emit('secureConnect');
        if (callback) {
            callback.call(socket);
        }
    });

    socket.connect({
        host: options.host ?? options.hostname,
        port: options.port,
    });

    return socket;
}

function createSecureContext(options = {}) {
    return {
        context: null,
        ...options,
    };
}

export {
    TLSSocket,
    connect,
    createSecureContext,
};

export default {
    TLSSocket,
    connect,
    createSecureContext,
};
