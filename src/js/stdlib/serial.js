import EventEmitter from 'tjs:events';
import FFI from 'tjs:ffi';
import { PosixSocket } from 'tjs:posix-socket';

const SERIAL_FLAG_RTSCTS = 0x01;

function getPlatform() {
    return navigator.userAgentData?.platform ?? 'Linux';
}

function getSharedLibraryExt() {
    switch (getPlatform()) {
        case 'Windows':
            return 'dll';
        case 'macOS':
            return 'dylib';
        default:
            return 'so';
    }
}

function errnoCode(errno) {
    switch (errno) {
        case 95:
        case 45:
            return 'ENOTSUP';
        case 11:
        case 35:
            return 'EAGAIN';
        case 2:
            return 'ENOENT';
        case 13:
            return 'EACCES';
        case 16:
            return 'EBUSY';
        case 5:
            return 'EIO';
        case 22:
            return 'EINVAL';
        default:
            return `ERRNO_${errno}`;
    }
}

function normalizeData(input) {
    if (typeof input === 'string') {
        return new TextEncoder().encode(input);
    }
    if (input instanceof Uint8Array) {
        return input;
    }
    if (ArrayBuffer.isView(input)) {
        return new Uint8Array(input.buffer, input.byteOffset, input.byteLength);
    }
    if (input instanceof ArrayBuffer) {
        return new Uint8Array(input);
    }
    throw new TypeError('write() expects string, ArrayBuffer, or ArrayBufferView');
}

function parityToCode(parity) {
    switch (parity) {
        case 'none':
            return 'n'.charCodeAt(0);
        case 'even':
            return 'e'.charCodeAt(0);
        case 'odd':
            return 'o'.charCodeAt(0);
        default:
            throw new TypeError(`unsupported parity: ${parity}`);
    }
}

function normalizeOptions(options = {}) {
    if (!options || typeof options !== 'object') {
        throw new TypeError('SerialPort options must be an object');
    }
    if (typeof options.path !== 'string' || options.path.length === 0) {
        throw new TypeError('SerialPort options.path must be a non-empty string');
    }

    const parity = options.parity ?? 'none';
    if (parity !== 'none' && parity !== 'even' && parity !== 'odd') {
        throw new TypeError('options.parity must be one of: none, even, odd');
    }

    return {
        path: options.path,
        baudRate: options.baudRate ?? 9600,
        dataBits: options.dataBits ?? 8,
        stopBits: options.stopBits ?? 1,
        parity,
        rtscts: options.rtscts === true,
        autoOpen: options.autoOpen !== false,
        readSize: options.readSize ?? 4096,
        nativePath: options.nativePath,
        rs485: options.rs485
    };
}

let nativeApi;

function loadNativeApi(explicitPath) {
    if (nativeApi) {
        return nativeApi;
    }

    const ext = getSharedLibraryExt();
    const candidates = [];
    if (explicitPath) {
        candidates.push(explicitPath);
    }
    if (typeof tjs?.env?.TJS_SERIAL_SHIM_PATH === 'string' && tjs.env.TJS_SERIAL_SHIM_PATH.length > 0) {
        candidates.push(tjs.env.TJS_SERIAL_SHIM_PATH);
    }
    candidates.push(`./build/libserial-shim.${ext}`);
    candidates.push(`libserial-shim.${ext}`);

    let lib;
    const errors = [];
    for (const candidate of candidates) {
        try {
            lib = new FFI.Lib(candidate);
            break;
        } catch (err) {
            errors.push(`${candidate}: ${err.message}`);
        }
    }
    if (!lib) {
        throw new Error(`failed to load serial native shim; tried ${errors.join(' | ')}`);
    }

    nativeApi = {
        serialOpen: new FFI.CFunction(
            lib.symbol('serial_open'),
            FFI.types.sint,
            [FFI.types.string, FFI.types.sint, FFI.types.sint, FFI.types.sint, FFI.types.sint, FFI.types.sint]
        ),
        serialClose: new FFI.CFunction(
            lib.symbol('serial_close'),
            FFI.types.sint,
            [FFI.types.sint]
        ),
        serialConfig: new FFI.CFunction(
            lib.symbol('serial_config'),
            FFI.types.sint,
            [FFI.types.sint, FFI.types.sint, FFI.types.sint, FFI.types.sint, FFI.types.sint, FFI.types.sint]
        ),
        serialDrain: new FFI.CFunction(
            lib.symbol('serial_drain'),
            FFI.types.sint,
            [FFI.types.sint]
        ),
        serialFlush: new FFI.CFunction(
            lib.symbol('serial_flush'),
            FFI.types.sint,
            [FFI.types.sint, FFI.types.sint]
        ),
        serialSetRs485: new FFI.CFunction(
            lib.symbol('serial_set_rs485'),
            FFI.types.sint,
            [FFI.types.sint, FFI.types.sint, FFI.types.sint, FFI.types.sint, FFI.types.sint, FFI.types.sint]
        ),
        serialStrerror: new FFI.CFunction(
            lib.symbol('serial_strerror'),
            FFI.types.string,
            [FFI.types.sint]
        )
    };

    return nativeApi;
}

function createNativeError(api, op, rc) {
    const errno = Math.abs(rc);
    const code = errnoCode(errno);
    let reason;
    try {
        reason = api.serialStrerror.call(errno);
    } catch {
        reason = `errno ${errno}`;
    }
    const err = new Error(`${op} failed (${code}): ${reason}`);
    err.errno = errno;
    err.code = code;
    return err;
}

function normalizeRs485Options(options = {}) {
    return {
        enabled: options.enabled !== false,
        rtsOnSend: options.rtsOnSend !== false,
        rtsAfterSend: options.rtsAfterSend === true,
        delayBeforeSendMs: options.delayBeforeSendMs ?? 0,
        delayAfterSendMs: options.delayAfterSendMs ?? 0
    };
}

function deferEvent(fn) {
    if (typeof process?.nextTick === 'function') {
        process.nextTick(fn);
        return;
    }
    if (typeof queueMicrotask === 'function') {
        queueMicrotask(fn);
        return;
    }
    setTimeout(fn, 0);
}

function sleep(ms) {
    return new Promise(resolve => setTimeout(resolve, ms));
}

export class SerialPort extends EventEmitter {
    constructor(options = {}) {
        super();
        if (!PosixSocket) {
            throw new Error('tjs:serial is only supported on Unix-like platforms');
        }
        this._opts = normalizeOptions(options);
        this._api = loadNativeApi(this._opts.nativePath);
        this._fd = -1;
        this._socket = null;
        this._closed = true;
        this._opening = false;
        this._closing = false;
        this._onPollReadable = this._onPollReadable.bind(this);
        this._onPollDisconnect = this._onPollDisconnect.bind(this);
        this._onPollError = this._onPollError.bind(this);

        if (this._opts.autoOpen) {
            this.open().catch(err => this.emit('error', err));
        }
    }

    get path() {
        return this._opts.path;
    }

    get isOpen() {
        return !this._closed && this._fd >= 0;
    }

    get fd() {
        return this._fd;
    }

    _flags() {
        return this._opts.rtscts ? SERIAL_FLAG_RTSCTS : 0;
    }

    _emitNativeError(op, rc) {
        const err = createNativeError(this._api, op, rc);
        this.emit('error', err);
        return err;
    }

    _isWouldBlockError(err) {
        if (!err) {
            return false;
        }
        if (err.code === 'EAGAIN' || err.code === 'EWOULDBLOCK') {
            return true;
        }
        if (err.errno === 11 || err.errno === 35) {
            return true;
        }
        const message = String(err.message ?? '');
        return message.includes('Resource temporarily unavailable') ||
            message.includes('temporarily unavailable') ||
            message.includes('(11)') ||
            message.includes('(35)');
    }

    async open() {
        if (this.isOpen) {
            return this;
        }
        if (this._opening) {
            return this;
        }

        this._opening = true;
        try {
            const rc = this._api.serialOpen.call(
                this._opts.path,
                this._opts.baudRate,
                this._opts.dataBits,
                this._opts.stopBits,
                parityToCode(this._opts.parity),
                this._flags()
            );
            if (rc < 0) {
                throw createNativeError(this._api, 'open', rc);
            }

            this._fd = rc;
            this._socket = PosixSocket.createFromFD(this._fd);
            this._closed = false;
            this._startPolling();

            if (this._opts.rs485) {
                await this.setRS485(this._opts.rs485);
            }
            deferEvent(() => this.emit('open'));
            return this;
        } catch (err) {
            if (this._socket) {
                try {
                    if (this._socket.polling) {
                        this._socket.stopPoll();
                    }
                } catch {
                }
                try {
                    this._socket.close();
                } catch {
                }
                this._socket = null;
            } else if (this._fd >= 0) {
                try {
                    this._api.serialClose.call(this._fd);
                } catch {
                }
            }
            this._fd = -1;
            this._closed = true;
            throw err;
        } finally {
            this._opening = false;
        }
    }

    _startPolling() {
        if (!this._socket) {
            return;
        }
        this._socket.poll({
            read: this._onPollReadable,
            disconnect: this._onPollDisconnect,
            error: this._onPollError
        });
    }

    _onPollReadable() {
        if (!this._socket || !this.isOpen) {
            return;
        }
        try {
            const chunk = this._socket.read(this._opts.readSize);
            if (chunk === null) {
                setTimeout(() => this.close().catch(err => this.emit('error', err)), 0);
                return;
            }
            if (chunk.length > 0) {
                this.emit('data', Buffer.from(chunk));
            }
        } catch (err) {
            this.emit('error', err);
        }
    }

    _onPollDisconnect() {
        setTimeout(() => this.close().catch(err => this.emit('error', err)), 0);
    }

    _onPollError(status) {
        const err = new Error(`serial poll failed: ${status}`);
        err.code = 'EPOLL';
        this.emit('error', err);
    }

    async close() {
        if (!this.isOpen || this._closing) {
            return;
        }

        this._closing = true;
        try {
            let deferClose = false;
            if (this._socket?.polling) {
                try {
                    this._socket.stopPoll();
                } catch (err) {
                    // If close() gets called from a poll callback, stop it on the next tick.
                    if (String(err?.message ?? '').includes('during callback')) {
                        deferClose = true;
                    } else {
                        this.emit('error', err);
                    }
                }
            }
            if (deferClose) {
                setTimeout(() => this.close().catch(err => this.emit('error', err)), 0);
                return;
            }
            if (this._socket) {
                try {
                    this._socket.close();
                } catch (err) {
                    if (String(err?.message ?? '').includes('during callback')) {
                        setTimeout(() => this.close().catch(e => this.emit('error', e)), 0);
                        return;
                    }
                    throw err;
                }
                this._socket = null;
            } else if (this._fd >= 0) {
                const rc = this._api.serialClose.call(this._fd);
                if (rc < 0) {
                    this._emitNativeError('close', rc);
                }
            }

            this._fd = -1;
            this._closed = true;
            this.emit('close');
        } finally {
            this._closing = false;
        }
    }

    async write(data) {
        if (!this.isOpen || !this._socket) {
            throw new Error('port is not open');
        }
        const buf = normalizeData(data);
        let offset = 0;
        let retries = 0;
        while (offset < buf.length) {
            let written;
            try {
                written = this._socket.write(buf.subarray(offset));
            } catch (err) {
                if (!this._isWouldBlockError(err)) {
                    throw err;
                }
                await sleep(Math.min(10, retries + 1));
                retries++;
                continue;
            }
            if (written <= 0) {
                throw new Error('write() returned 0 bytes');
            }
            offset += written;
            retries = 0;
        }
        return offset;
    }

    async drain() {
        if (!this.isOpen) {
            throw new Error('port is not open');
        }
        const rc = this._api.serialDrain.call(this._fd);
        if (rc < 0) {
            throw createNativeError(this._api, 'drain', rc);
        }
    }

    async flush(queue = 'both') {
        if (!this.isOpen) {
            throw new Error('port is not open');
        }
        let selector = 0;
        if (queue === 'input') {
            selector = 1;
        } else if (queue === 'output') {
            selector = 2;
        } else if (queue !== 'both') {
            throw new TypeError("flush() queue must be 'both', 'input', or 'output'");
        }
        const rc = this._api.serialFlush.call(this._fd, selector);
        if (rc < 0) {
            throw createNativeError(this._api, 'flush', rc);
        }
    }

    async update(options = {}) {
        if (!this.isOpen) {
            throw new Error('port is not open');
        }

        const next = { ...this._opts, ...options };
        this._opts = normalizeOptions(next);

        const rc = this._api.serialConfig.call(
            this._fd,
            this._opts.baudRate,
            this._opts.dataBits,
            this._opts.stopBits,
            parityToCode(this._opts.parity),
            this._flags()
        );
        if (rc < 0) {
            throw createNativeError(this._api, 'update', rc);
        }
    }

    async setRS485(options = {}) {
        if (!this.isOpen) {
            throw new Error('port is not open');
        }
        const rs485 = normalizeRs485Options(options);
        const rc = this._api.serialSetRs485.call(
            this._fd,
            rs485.enabled ? 1 : 0,
            rs485.rtsOnSend ? 1 : 0,
            rs485.rtsAfterSend ? 1 : 0,
            rs485.delayBeforeSendMs,
            rs485.delayAfterSendMs
        );
        if (rc < 0) {
            throw createNativeError(this._api, 'setRS485', rc);
        }
    }
}

export default SerialPort;
