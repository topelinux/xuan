import EventEmitter from 'tjs:events';
const textEncoder = new TextEncoder();
const textDecoder = new TextDecoder();

function isUint8Array(value) {
    return value instanceof Uint8Array;
}

function normalizeChunk(chunk, encoding, objectMode) {
    if (objectMode) return chunk;
    if (typeof chunk === 'string') {
        if (encoding && encoding !== 'utf8' && encoding !== 'utf-8') {
            throw new TypeError(`Unsupported encoding: ${encoding}`);
        }
        return textEncoder.encode(chunk);
    }
    if (isUint8Array(chunk)) return chunk;
    throw new TypeError('chunk must be string or Uint8Array');
}

function normalizeWriteArgs(chunk, encoding, callback) {
    if (typeof encoding === 'function') {
        callback = encoding;
        encoding = undefined;
    }
    return { chunk, encoding, callback };
}

function Stream() {
    if (!(this instanceof Stream)) {
        return new Stream();
    }
    EventEmitter.call(this);
    return this;
}

Stream.prototype = Object.create(EventEmitter.prototype);
Stream.prototype.constructor = Stream;

class Readable extends Stream {
    constructor(options = {}) {
        super();
        this.readable = true;
        this._objectMode = !!options.objectMode;
        this._highWaterMark = options.highWaterMark ?? 16 * 1024;
        this._buffer = [];
        this._bufferSize = 0;
        this._flowing = null;
        this._readableEnded = false;
        this._readableEndEmitted = false;
        this._destroyed = false;
        this._encoding = options.encoding ?? null;
        this._emitClose = options.emitClose !== false;
        if (typeof options.read === 'function') {
            this._read = options.read;
        }
    }

    on(eventName, listener) {
        const res = EventEmitter.prototype.on.call(this, eventName, listener);
        if (eventName === 'data') {
            this.resume();
        }
        return res;
    }

    addListener(eventName, listener) {
        const res = EventEmitter.prototype.on.call(this, eventName, listener);
        if (eventName === 'data') {
            this.resume();
        }
        return res;
    }

    once(eventName, listener) {
        const res = EventEmitter.prototype.once.call(this, eventName, listener);
        if (eventName === 'data') {
            this.resume();
        }
        return res;
    }

    _addChunk(chunk) {
        this._buffer.push(chunk);
        if (!this._objectMode) {
            this._bufferSize += chunk.byteLength;
        }
    }

    _shiftChunk(size) {
        if (this._buffer.length === 0) return null;
        if (this._objectMode) {
            return this._buffer.shift();
        }
        const first = this._buffer[0];
        if (size == null || first.byteLength <= size) {
            this._buffer.shift();
            this._bufferSize -= first.byteLength;
            return first;
        }
        const out = first.subarray(0, size);
        this._buffer[0] = first.subarray(size);
        this._bufferSize -= out.byteLength;
        return out;
    }

    _emitData() {
        while (this._flowing && this._buffer.length > 0) {
            const chunk = this._shiftChunk();
            if (!this._objectMode && this._encoding) {
                this.emit('data', textDecoder.decode(chunk));
            } else {
                this.emit('data', chunk);
            }
        }
        if (this._flowing && this._readableEnded && this._buffer.length === 0 && !this._readableEndEmitted) {
            this._readableEndEmitted = true;
            this.emit('end');
        }
    }

    push(chunk, encoding) {
        if (this._destroyed) return false;
        if (chunk === null) {
            this._readableEnded = true;
            if (this._flowing) {
                this._emitData();
            }
            // Don't emit 'end' here if not flowing - let resume()/read() handle it
            return false;
        }

        const normalized = normalizeChunk(chunk, encoding ?? this._encoding, this._objectMode);
        this._addChunk(normalized);

        if (this._flowing) {
            this._emitData();
        } else {
            this.emit('readable');
        }

        if (this._objectMode) return this._buffer.length < this._highWaterMark;
        return this._bufferSize < this._highWaterMark;
    }

    read(size) {
        const chunk = this._shiftChunk(size);
        if (chunk == null) return null;
        if (!this._objectMode && this._encoding) {
            return textDecoder.decode(chunk);
        }
        return chunk;
    }

    setEncoding(encoding) {
        if (encoding !== 'utf8' && encoding !== 'utf-8') {
            throw new TypeError(`Unsupported encoding: ${encoding}`);
        }
        this._encoding = encoding;
        return this;
    }

    pause() {
        this._flowing = false;
        return this;
    }

    resume() {
        if (this._flowing === false) {
            this._flowing = true;
            queueMicrotask(() => this._emitData());
        } else {
            this._flowing = true;
            // Process any buffered data and handle already-ended stream
            if (this._buffer.length > 0 || !this._readableEnded) {
                queueMicrotask(() => this._emitData());
            } else if (this._readableEnded && this._buffer.length === 0 && !this._readableEndEmitted) {
                queueMicrotask(() => {
                    this._readableEndEmitted = true;
                    this.emit('end');
                    if (this._emitClose) {
                        this._destroyed = true;
                        this.emit('close');
                    }
                });
            }
        }
        return this;
    }

    isPaused() {
        return this._flowing === false;
    }

    pipe(dest, options = {}) {
        this.resume();
        this.on('data', (chunk) => {
            const canWrite = dest.write ? dest.write(chunk) !== false : true;
            if (!canWrite && this.pause) {
                this.pause();
                dest.once?.('drain', () => this.resume());
            }
        });
        this.on('end', () => {
            if (options.end !== false && dest.end) {
                dest.end();
            }
        });
        this.on('error', (err) => dest.emit?.('error', err));
        
        // Handle already-ended stream with buffered data
        if (this._readableEnded && this._buffer.length > 0) {
            while (this._buffer.length > 0) {
                const chunk = this._shiftChunk();
                dest.write?.(chunk);
            }
            if (options.end !== false && dest.end) {
                dest.end();
            }
        }
        return dest;
    }

    unpipe(dest) {
        if (!dest) {
            this.removeAllListeners('data');
            return this;
        }
        this.removeAllListeners('data');
        return this;
    }

    destroy(err) {
        if (this._destroyed) return this;
        this._destroyed = true;
        if (err) this.emit('error', err);
        this.emit('close');
        return this;
    }
}

class Writable extends Stream {
    constructor(options = {}) {
        super();
        this.writable = true;
        this._objectMode = !!options.objectMode;
        this._highWaterMark = options.highWaterMark ?? 16 * 1024;
        this._writableEnded = false;
        this._destroyed = false;
        this._emitClose = options.emitClose !== false;
        if (typeof options.write === 'function') {
            this._write = options.write;
        }
    }

    write(chunk, encoding, callback) {
        if (this._destroyed) return false;
        if (this._writableEnded) {
            const err = new Error('write after end');
            this.emit('error', err);
            if (typeof callback === 'function') callback(err);
            return false;
        }

        const args = normalizeWriteArgs(chunk, encoding, callback);
        let normalized;
        try {
            normalized = normalizeChunk(args.chunk, args.encoding, this._objectMode);
        } catch (err) {
            this.emit('error', err);
            if (args.callback) args.callback(err);
            return false;
        }

        const onWriteDone = (err) => {
            if (err) {
                this.emit('error', err);
                if (args.callback) args.callback(err);
                return;
            }
            if (args.callback) args.callback(null);
        };

        if (typeof this._write === 'function') {
            try {
                this._write(normalized, args.encoding, onWriteDone);
            } catch (err) {
                this.emit('error', err);
                if (args.callback) args.callback(err);
                return false;
            }
        } else {
            queueMicrotask(() => {
                onWriteDone(null);
                this.emit('drain');
            });
        }
        return true;
    }

    end(chunk, encoding, callback) {
        const args = normalizeWriteArgs(chunk, encoding, callback);
        if (args.chunk !== undefined) {
            this.write(args.chunk, args.encoding);
        }
        this._writableEnded = true;
        queueMicrotask(() => {
            if (typeof this._final === 'function') {
                try {
                    this._final(args.callback ?? (() => {}));
                } catch (err) {
                    this.emit('error', err);
                    if (args.callback) args.callback(err);
                    return;
                }
            } else if (args.callback) {
                args.callback(null);
            }
            this.emit('finish');
            // Emit 'close' after 'finish' if emitClose is enabled
            if (this._emitClose) {
                this._destroyed = true;
                this.emit('close');
            }
        });
        return this;
    }

    destroy(err) {
        if (this._destroyed) return this;
        this._destroyed = true;
        if (err) this.emit('error', err);
        this.emit('close');
        return this;
    }
}

function initWritableState(target, options = {}) {
    target.writable = true;
    target._objectMode = !!options.objectMode;
    target._highWaterMark = options.highWaterMark ?? 16 * 1024;
    target._writableEnded = false;
    target._destroyed = false;
}

class Duplex extends Readable {
    constructor(options = {}) {
        super(options);
        initWritableState(this, options);
    }
}

for (const key of Object.getOwnPropertyNames(Writable.prototype)) {
    if (key === 'constructor') continue;
    if (!Object.prototype.hasOwnProperty.call(Duplex.prototype, key)) {
        Object.defineProperty(
            Duplex.prototype,
            key,
            Object.getOwnPropertyDescriptor(Writable.prototype, key),
        );
    }
}
for (const key of Object.getOwnPropertySymbols(Writable.prototype)) {
    if (!Object.prototype.hasOwnProperty.call(Duplex.prototype, key)) {
        Object.defineProperty(
            Duplex.prototype,
            key,
            Object.getOwnPropertyDescriptor(Writable.prototype, key),
        );
    }
}

class Transform extends Duplex {
    constructor(options = {}) {
        super(options);
    }

    _transform(chunk, _encoding, callback) {
        callback(null, chunk);
    }

    write(chunk, encoding, callback) {
        const args = normalizeWriteArgs(chunk, encoding, callback);
        let normalized;
        try {
            normalized = normalizeChunk(args.chunk, args.encoding, this._objectMode);
        } catch (err) {
            this.emit('error', err);
            if (args.callback) args.callback(err);
            return false;
        }
        this._transform(normalized, args.encoding, (err, data) => {
            if (err) {
                this.emit('error', err);
                if (args.callback) args.callback(err);
                return;
            }
            if (data !== undefined) {
                this.push(data);
            }
            if (args.callback) args.callback(null);
        });
        return true;
    }
}

class PassThrough extends Transform {
    _transform(chunk, _encoding, callback) {
        callback(null, chunk);
    }
}

function readableFrom(iterable, options = {}) {
    const readable = new Readable({
        objectMode: options.objectMode ?? true,
        highWaterMark: options.highWaterMark,
    });

    if (iterable[Symbol.asyncIterator]) {
        (async () => {
            try {
                for await (const chunk of iterable) {
                    readable.push(chunk);
                }
                readable.push(null);
            } catch (err) {
                readable.destroy(err);
            }
        })();
    } else if (iterable[Symbol.iterator]) {
        (() => {
            try {
                for (const chunk of iterable) {
                    readable.push(chunk);
                }
                readable.push(null);
            } catch (err) {
                readable.destroy(err);
            }
        })();
    } else {
        readable.destroy(new TypeError('argument must be iterable'));
    }

    return readable;
}

Readable.from = readableFrom;

function finished(stream, callback) {
    const onFinish = () => cleanup();
    const onEnd = () => cleanup();
    const onError = (err) => cleanup(err);

    const cleanup = (err) => {
        stream.off?.('finish', onFinish);
        stream.off?.('end', onEnd);
        stream.off?.('error', onError);
        if (typeof callback === 'function') callback(err);
    };

    stream.on?.('finish', onFinish);
    stream.on?.('end', onEnd);
    stream.on?.('error', onError);
    return stream;
}

function pipeline(...args) {
    const callback = typeof args[args.length - 1] === 'function' ? args.pop() : null;
    const streams = args;
    if (streams.length < 2) {
        const err = new Error('pipeline requires at least 2 streams');
        if (callback) callback(err);
        else throw err;
        return streams[0];
    }

    for (let i = 0; i < streams.length - 1; i++) {
        streams[i].pipe(streams[i + 1]);
    }

    const last = streams[streams.length - 1];
    const done = (err) => {
        if (callback) callback(err);
    };

    streams.forEach((s) => s.on?.('error', done));
    finished(last, done);

    return last;
}

Stream.Stream = Stream;
Stream.Readable = Readable;
Stream.Writable = Writable;
Stream.Duplex = Duplex;
Stream.Transform = Transform;
Stream.PassThrough = PassThrough;
Stream.pipeline = pipeline;
Stream.finished = finished;

export {
    Stream,
    Readable,
    Writable,
    Duplex,
    Transform,
    PassThrough,
    pipeline,
    finished,
};

export default Stream;
