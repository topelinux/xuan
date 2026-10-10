import EventEmitter from 'tjs:events';
import { Buffer } from 'tjs:buffer';

const core = globalThis[Symbol.for('tjs.internal.core')];

const defaultConnectOptions = {
    keepalive: 10,
    reconnectPeriod: 1000,
    connectTimeout: 30 * 1000,
    clean: true,
    queueQoSZero: true,
};

const kNative = Symbol('kNative');
const kPending = Symbol('kPending');
const kQueue = Symbol('kQueue');
const nativeSetTimeout = globalThis.setTimeout;
const nativeClearTimeout = globalThis.clearTimeout;

function defaultId() {
    return `mqttjs_${Math.random().toString(16).slice(2, 10)}`;
}

function normalizePayload(message) {
    if (Buffer.isBuffer(message)) {
        return message;
    }

    if (message instanceof Uint8Array) {
        return Buffer.from(message.buffer, message.byteOffset, message.byteLength);
    }

    if (ArrayBuffer.isView(message)) {
        return Buffer.from(message.buffer, message.byteOffset, message.byteLength);
    }

    if (message instanceof ArrayBuffer) {
        return Buffer.from(message);
    }

    return Buffer.from(String(message ?? ''));
}

function normalizeSubscribeTopics(topic, opts = { qos: 0 }) {
    if (typeof topic === 'string') {
        return [{ topic, qos: opts.qos ?? 0 }];
    }

    if (Array.isArray(topic)) {
        return topic.map((item) => ({ topic: String(item), qos: opts.qos ?? 0 }));
    }

    return Object.keys(topic).map((name) => ({ topic: name, qos: topic[name] ?? 0 }));
}

function normalizeUnsubscribeTopics(topic) {
    if (typeof topic === 'string') {
        return [topic];
    }

    if (Array.isArray(topic)) {
        return topic.map((item) => String(item));
    }

    return [];
}

function parseAuthOptions(opts) {
    const next = { ...opts };

    if (next.auth) {
        const matches = next.auth.match(/^(.+):(.+)$/);
        if (matches) {
            next.username = matches[1];
            next.password = matches[2];
        } else {
            next.username = next.auth;
        }
    }

    return next;
}

function parseConnectArgs(input, overrides = undefined) {
    let base = {};

    if (typeof input === 'string') {
        const url = new URL(input);
        base.protocol = normalizeProtocol(url.protocol.replace(/:$/, ''));
        base.host = url.hostname;
        base.hostname = url.hostname;
        if (url.port) {
            base.port = Number(url.port);
        }
        const path = `${url.pathname || ''}${url.search || ''}`;
        if (path && path !== '/') {
            base.path = path;
        }
        if (url.username) {
            base.username = decodeURIComponent(url.username);
        }
        if (url.password) {
            base.password = decodeURIComponent(url.password);
        }
    } else if (input && typeof input === 'object') {
        base = input;
    }

    const next = parseAuthOptions({ ...base, ...(overrides || {}) });
    next.protocol = normalizeProtocol(next.protocol);

    if ((next.protocol === 'ws' || next.protocol === 'wss') && !next.path && next.pathname) {
        next.path = next.pathname;
    }
    if ((next.protocol === 'ws' || next.protocol === 'wss') && !next.path) {
        next.path = '/mqtt';
    }
    if (next.port === undefined) {
        if (next.protocol === 'ws') {
            next.port = 80;
        } else if (next.protocol === 'wss') {
            next.port = 443;
        }
    }

    return next;
}

function normalizeProtocol(protocol) {
    switch (protocol) {
        case 'mqtt+ws':
            return 'ws';
        case 'mqtt+wss':
            return 'wss';
        default:
            return protocol;
    }
}

export class MqttClient extends EventEmitter {
    constructor(options = {}) {
        super();
        this.options = { ...defaultConnectOptions, ...options };
        this.options.clientId = this.options.clientId || defaultId();
        this.connected = false;
        this.disconnecting = false;
        this.reconnecting = false;
        this.reconnectTimer = null;
        this.nextId = Math.floor(Math.random() * 65535) || 1;
        this._reconnectCount = 0;
        this.connectTimer = null;
        this[kPending] = [];
        this[kQueue] = [];

        this._connect();
    }

    _resolveConnectOptions() {
        const opts = { ...this.options };

        if (opts.servers?.length) {
            if (!this._reconnectCount || this._reconnectCount === opts.servers.length) {
                this._reconnectCount = 0;
            }

            const server = opts.servers[this._reconnectCount];
            opts.host = server.host;
            opts.hostname = server.host;
            opts.port = server.port;
            this._reconnectCount++;
        }

        return opts;
    }

    _connect() {
        const NativeMQTTClient = core.MQTTClient;
        let raw;
        let sawError = false;
        let sawConnect = false;
        this._clearConnectTimeout();
        try {
            raw = new NativeMQTTClient(this._resolveConnectOptions());
        } catch (error) {
            const err = error instanceof Error ? error : new Error(String(error || 'MQTT connect failed'));
            this.connected = false;
            this[kNative] = null;
            this._rejectPending(err);

            queueMicrotask(() => {
                if (this.disconnecting) {
                    return;
                }

                this.emit('error', err);
                this.emit('close');
                this._setupReconnect();
            });
            return;
        }

        this[kNative] = raw;
        raw.onconnect = (packet) => {
            sawConnect = true;
            this._clearConnectTimeout();
            this.connected = true;
            this.reconnecting = false;
            this._clearReconnect();
            this.emit('connect', packet);
            this._flushQueue();
        };
        raw.onmessage = (topic, payload, qos, retain) => {
            const message = Buffer.from(payload);
            this.emit('message', topic, message, {
                topic,
                payload: message,
                qos,
                retain,
            });
        };
        raw.onack = () => {
            this._settlePending('ack');
        };
        raw.onsubscribed = (error) => {
            this._settlePending('subscribed', error);
        };
        raw.onunsubscribed = () => {
            this._settlePending('unsubscribed');
        };
        raw.onerror = (error) => {
            sawError = true;
            const err = error instanceof Error ? error : new Error(String(error || 'MQTT error'));
            this.emit('error', err);
        };
        raw.onclose = () => {
            this._clearConnectTimeout();
            if (!this.disconnecting && !this.connected && !sawConnect && !sawError) {
                this.emit('error', new Error('MQTT connection closed before connect'));
            }
            this.connected = false;
            this[kNative] = null;
            this.emit('close');
            this._rejectPending(new Error('MQTT connection closed'));
            this._setupReconnect();
        };

        // Native connect / close can happen before handlers are attached.
        // Sync once to avoid losing early state transitions.
        const nativeState = raw.debugInfo;
        if (nativeState?.connected && !this.connected) {
            queueMicrotask(() => {
                if (this[kNative] !== raw || this.connected) {
                    return;
                }

                this.connected = true;
                this.reconnecting = false;
                this._clearReconnect();
                this.emit('connect', { returnCode: 0, sessionPresent: false });
                this._flushQueue();
            });
        } else if (nativeState?.closed) {
            queueMicrotask(() => {
                if (this[kNative] !== raw) {
                    return;
                }

                this.connected = false;
                this[kNative] = null;
                this.emit('close');
                this._rejectPending(new Error('MQTT connection closed'));
                this._setupReconnect();
            });
        }

        this._armConnectTimeout(raw);
    }

    _nextId() {
        const id = this.nextId++;
        if (this.nextId > 65535) {
            this.nextId = 1;
        }
        return id;
    }

    _clearReconnect() {
        if (!this.reconnectTimer) {
            return;
        }

        clearTimeout(this.reconnectTimer);
        this.reconnectTimer = null;
    }

    _clearConnectTimeout() {
        if (!this.connectTimer) {
            return;
        }

        nativeClearTimeout(this.connectTimer);
        this.connectTimer = null;
    }

    _armConnectTimeout(raw) {
        const timeout = Number(this.options.connectTimeout ?? 0);
        if (!Number.isFinite(timeout) || timeout <= 0) {
            return;
        }

        this.connectTimer = nativeSetTimeout(() => {
            this.connectTimer = null;
            if (this[kNative] !== raw || this.connected || this.disconnecting) {
                return;
            }

            const err = new Error(`MQTT connect timeout after ${timeout}ms`);
            this.emit('error', err);

            if (this[kNative] === raw && typeof raw.close === 'function') {
                raw.close();
            }
        }, timeout);
    }

    _setupReconnect() {
        const reconnectPeriod = Number(this.options.reconnectPeriod);
        if (this.disconnecting || this.reconnectTimer || !Number.isFinite(reconnectPeriod) || reconnectPeriod <= 0) {
            return;
        }

        if (!this.reconnecting) {
            this.emit('offline');
            this.reconnecting = true;
        }

        this.reconnectTimer = setTimeout(() => {
            this.reconnectTimer = null;
            if (this.disconnecting) {
                return;
            }
            this.emit('reconnect');
            this._connect();
        }, reconnectPeriod);
    }

    _flushQueue() {
        while (this.connected && this[kQueue].length > 0) {
            const entry = this[kQueue].shift();
            entry();
        }
    }

    _enqueue(fn) {
        if (this.connected) {
            fn();
            return;
        }

        this[kQueue].push(fn);
    }

    _sendOrQueue(fn, qos, callback) {
        if (this.disconnecting) {
            callback?.(new Error('client disconnecting'));
            return this;
        }

        if (!this.connected) {
            if (qos > 0 || this.options.queueQoSZero) {
                this[kQueue].push(fn);
            } else {
                callback?.(new Error('No connection to broker'));
            }
            return this;
        }

        fn();
        return this;
    }

    _settlePending(kind, error) {
        const entry = this[kPending].shift();
        if (!entry) {
            return;
        }

        if (error) {
            const err = error instanceof Error ? error : new Error(String(error));
            entry.callback?.(err);
            this._maybeFinishDisconnect();
            return;
        }

        switch (kind) {
            case 'subscribed':
                entry.callback?.(null, entry.granted);
                break;
            case 'unsubscribed':
            case 'ack':
                entry.callback?.(null);
                break;
        }

        this._maybeFinishDisconnect();
    }

    _rejectPending(error) {
        while (this[kPending].length > 0) {
            const entry = this[kPending].shift();
            entry.callback?.(error);
        }
    }

    _maybeFinishDisconnect() {
        if (!this.disconnecting || this[kPending].length > 0 || this[kQueue].length > 0) {
            return;
        }

        this._endCallback?.();
        this._endCallback = null;
    }

    get debugInfo() {
        return {
            jsPending: this[kPending].length,
            jsQueue: this[kQueue].length,
            connected: this.connected,
            disconnecting: this.disconnecting,
            reconnecting: this.reconnecting,
            reconnectTimerActive: this.reconnectTimer != null,
            connectTimerActive: this.connectTimer != null,
            native: this[kNative]?.debugInfo ?? null,
        };
    }

    publish(topic, message, opts, callback) {
        if (typeof opts === 'function') {
            callback = opts;
            opts = null;
        }

        const options = { qos: 0, retain: false, ...(opts || {}) };
        const payload = normalizePayload(message);
        const packetId = this._nextId();
        const action = () => {
            this[kNative].publish(topic, payload, options.qos, options.retain, packetId);
            if (options.qos > 0) {
                this[kPending].push({ type: 'publish', callback });
            } else {
                callback?.(null);
            }
        };

        return this._sendOrQueue(action, options.qos, callback);
    }

    subscribe(topic, opts, callback) {
        if (typeof opts === 'function') {
            callback = opts;
            opts = null;
        }

        if (typeof callback !== 'function') {
            callback = undefined;
        }

        const subscriptions = normalizeSubscribeTopics(topic, opts || { qos: 0 });
        const granted = subscriptions.map((item) => ({ topic: item.topic, qos: item.qos }));
        const packetId = this._nextId();
        const action = () => {
            this[kPending].push({ type: 'subscribe', callback, granted });
            this[kNative].subscribe(subscriptions, packetId);
        };

        return this._sendOrQueue(action, 1, callback);
    }

    unsubscribe(topic, callback) {
        const topics = normalizeUnsubscribeTopics(topic);
        const packetId = this._nextId();
        const action = () => {
            this[kPending].push({ type: 'unsubscribe', callback });
            this[kNative].unsubscribe(topics, packetId);
        };

        return this._sendOrQueue(action, 1, callback);
    }

    end(force, cb) {
        if (typeof force === 'function') {
            cb = force;
            force = false;
        }

        if (this.disconnecting) {
            return this;
        }

        this.disconnecting = true;
        this.reconnecting = false;
        this._clearReconnect();
        this._clearConnectTimeout();

        if (cb) {
            this.once('close', cb);
        }

        if (force || this[kPending].length === 0) {
            this[kNative]?.close();
        } else {
            this._endCallback = () => {
                this[kNative]?.close();
            };
        }

        return this;
    }
}

export function connect(urlOrOpts = {}, opts = undefined) {
    const options = parseConnectArgs(urlOrOpts, opts);

    if (options.clean === false && !options.clientId) {
        throw new Error('Missing clientId for unclean clients');
    }

    return new MqttClient(options);
}

const mqtt = {
    connect,
    MqttClient,
    QoS: {
        QOS0: 0,
        QOS1: 1,
        QOS2: 2,
    },
};

export default mqtt;
