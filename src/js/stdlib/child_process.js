const core = globalThis[Symbol.for('tjs.internal.core')];
const EventEmitter = require('node:events');
const { Buffer } = require('node:buffer');

const kProcess = Symbol('kProcess');
const kStdin = Symbol('kStdin');
const kStdout = Symbol('kStdout');
const kStderr = Symbol('kStderr');

function silentClose(handle) {
    try {
        handle.close();
    } catch {
        // Ignored.
    }
}

function normalizeChunk(chunk, encoding = 'utf8') {
    if (typeof chunk === 'string') {
        if (encoding !== 'utf8' && encoding !== 'utf-8') {
            throw new TypeError(`Unsupported encoding: ${encoding}`);
        }

        return new TextEncoder().encode(chunk);
    }

    if (chunk instanceof Uint8Array) {
        return chunk;
    }

    if (ArrayBuffer.isView(chunk)) {
        return new Uint8Array(chunk.buffer, chunk.byteOffset, chunk.byteLength);
    }

    if (chunk instanceof ArrayBuffer) {
        return new Uint8Array(chunk);
    }

    throw new TypeError('The "chunk" argument must be of type string or an ArrayBuffer/ArrayBufferView');
}

function toReadableChunk(chunk) {
    if (chunk == null) {
        return chunk;
    }

    if (Buffer.isBuffer(chunk)) {
        return chunk;
    }

    if (chunk instanceof Uint8Array) {
        return Buffer.from(chunk.buffer, chunk.byteOffset, chunk.byteLength);
    }

    if (ArrayBuffer.isView(chunk)) {
        return Buffer.from(chunk.buffer, chunk.byteOffset, chunk.byteLength);
    }

    if (chunk instanceof ArrayBuffer) {
        return Buffer.from(chunk);
    }

    return chunk;
}

class ProcessReadableStream extends EventEmitter {
    constructor(handle) {
        super();
        this._handle = handle;
        this._controller = null;
        this._reading = false;
        this._closed = false;
        this.ended = false;
        this.stream = new ReadableStream({
            start: (controller) => {
                this._controller = controller;
                handle.onread = (data, error) => {
                    if (error) {
                        this._stopRead();
                        this._closed = true;
                        handle.onread = null;
                        controller.error(error);
                        silentClose(handle);
                    } else if (data === null) {
                        this._stopRead();
                        this._closed = true;
                        this.ended = true;
                        handle.onread = null;
                        controller.close();
                        silentClose(handle);
                    } else {
                        controller.enqueue(data);

                        if (controller.desiredSize <= 0) {
                            this._stopRead();
                        }
                    }
                };
            },
            pull: (controller) => {
                if (this._closed) {
                    try {
                        controller.close();
                    } catch {
                        // Ignored.
                    }
                    return;
                }

                if (!this._reading) {
                    this._reading = true;
                    try {
                        handle.startRead();
                    } catch (e) {
                        // If the pipe is no longer connected/readable (e.g., child exited
                        // before startRead was called), treat it as EOF rather than throwing.
                        if (e?.code === 'ENOTCONN') {
                            this._reading = false;
                            this._closed = true;
                            this.ended = true;
                            handle.onread = null;
                            silentClose(handle);
                            controller.close();
                        } else {
                            throw e;
                        }
                    }
                }
            },
            cancel: () => {
                this._closeHandle();
            }
        });

        this.flowing = false;
        this.flowReader = null;
    }

    _stopRead() {
        if (!this._reading) {
            return;
        }

        try {
            this._handle.stopRead();
        } catch {
            // Ignored.
        }
        this._reading = false;
    }

    _closeHandle() {
        if (this._closed) {
            return;
        }

        this._stopRead();
        this._closed = true;
        this._handle.onread = null;
        silentClose(this._handle);
    }

    _closeAfterExit() {
        if (this.ended || this.flowing || this.stream.locked) {
            return;
        }

        this.ended = true;
        this._closeHandle();
        try {
            this._controller?.close();
        } catch {
            // The stream may already be closed or errored.
        }
    }

    getReader() {
        return this.stream.getReader();
    }

    get locked() {
        return this.stream.locked;
    }

    cancel(reason) {
        return this.stream.cancel(reason);
    }

    async bytes() {
        const reader = this.getReader();
        const chunks = [];
        let totalLength = 0;

        while (true) {
            const { done, value } = await reader.read();
            if (done) {
                break;
            }

            chunks.push(value);
            totalLength += value.byteLength;
        }

        const result = new Uint8Array(totalLength);
        let offset = 0;
        for (const chunk of chunks) {
            result.set(chunk, offset);
            offset += chunk.byteLength;
        }
        return result;
    }

    async text() {
        const bytes = await this.bytes();
        return new TextDecoder().decode(bytes);
    }

    async arrayBuffer() {
        const bytes = await this.bytes();
        return bytes.buffer;
    }

    on(eventName, listener) {
        const ret = super.on(eventName, listener);

        if (eventName === 'data') {
            this.startFlowing();
        }

        return ret;
    }

    startFlowing() {
        if (this.flowing || this.ended) {
            return;
        }

        this.flowing = true;
        this.flowReader = this.getReader();
        this.pumpFlow();
    }

    async pumpFlow() {
        try {
            while (this.flowing) {
                const { done, value } = await this.flowReader.read();
                if (done) {
                    this.ended = true;
                    this.flowing = false;
                    this.emit('end');
                    this.emit('close');
                    break;
                }

                this.emit('data', toReadableChunk(value));
            }
        } catch (error) {
            this.flowing = false;
            this.emit('error', error);
        }
    }
}

class ProcessWritableStream {
    constructor(handle) {
        const queue = [];
        this._handle = handle;
        this._closed = false;

        handle.onwrite = error => {
            const entry = queue.shift();

            if (entry) {
                if (error) {
                    entry.reject(error);
                } else {
                    entry.resolve();
                }
            }
        };

        this.stream = new WritableStream({
            async write(chunk, controller) {
                try {
                    const result = handle.write(chunk);

                    if (typeof result !== 'number') {
                        const { promise, resolve, reject } = Promise.withResolvers();

                        queue.push({ resolve, reject });
                        await promise;
                    }
                } catch (e) {
                    controller.error(e);
                }
            },
            close: () => {
                this._closeHandle();
            },
            abort: () => {
                this._closeHandle();
            }
        });

        this.writer = this.stream.getWriter();
        this.writableEnded = false;
        this.writableFinished = false;
        this.destroyed = false;
    }

    _closeHandle() {
        if (this._closed) {
            return;
        }

        this._closed = true;
        silentClose(this._handle);
    }

    _closeAfterExit() {
        this.destroy();
    }

    getWriter() {
        return this.stream.getWriter();
    }

    get locked() {
        return this.stream.locked;
    }

    abort(reason) {
        return this.stream.abort(reason);
    }

    write(chunk, encoding, callback) {
        if (typeof encoding === 'function') {
            callback = encoding;
            encoding = undefined;
        }

        if (this.writableEnded) {
            const error = new Error('write after end');
            if (callback) {
                callback(error);
            }
            return false;
        }

        const data = normalizeChunk(chunk, encoding);

        this.writer.write(data).then(() => {
            if (callback) {
                callback();
            }
        }).catch(error => {
            if (callback) {
                callback(error);
            }
        });

        return true;
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

        let promise = Promise.resolve();

        if (chunk !== undefined && chunk !== null) {
            promise = this.writer.write(normalizeChunk(chunk, encoding));
        }

        this.writableEnded = true;
        promise = promise.then(() => this.writer.close());
        promise.then(() => {
            this.writableFinished = true;
            if (callback) {
                callback();
            }
        }).catch(error => {
            if (callback) {
                callback(error);
            }
        });

        return this;
    }

    destroy(error) {
        if (this.destroyed) {
            return this;
        }

        this.destroyed = true;
        this.writableEnded = true;
        this.writer.abort(error).catch(() => {});
        this._closeHandle();
        return this;
    }
}

class Subprocess extends EventEmitter {
    constructor(proc, stdin, stdout, stderr) {
        super();
        this[kProcess] = proc;
        this[kStdin] = stdin ?? null;
        this[kStdout] = stdout ?? null;
        this[kStderr] = stderr ?? null;

        if (proc && typeof proc.wait === 'function') {
            // Handle the exit event
            proc.wait().then(result => {
                this.emit('exit', result.exit_status, result.term_signal || null);
                this[kStdin]?._closeAfterExit?.();
                this[kStdout]?._closeAfterExit?.();
                this[kStderr]?._closeAfterExit?.();

                // Emit the close event after process exits
                process.nextTick(() => {
                    this.emit('close', result.exit_status, result.term_signal || null);
                });
            }).catch(error => {
                this[kStdin]?._closeAfterExit?.();
                this[kStdout]?._closeAfterExit?.();
                this[kStderr]?._closeAfterExit?.();
                this.emit('error', error);
            });
        }
    }

    get pid() {
        return this[kProcess].pid;
    }

    get stdin() {
        return this[kStdin];
    }

    get stdout() {
        return this[kStdout];
    }

    get stderr() {
        return this[kStderr];
    }

    kill(sig) {
        try {
            this[kProcess].kill(sig);
            return true;
        } catch (error) {
            if (error?.code === 'ESRCH') {
                return false;
            }
            throw error;
        }
    }

    wait() {
        return this[kProcess].wait();
    }
}

export function spawn(command, args, options) {
    let cmdArgs;
    if (Array.isArray(command)) {
        cmdArgs = command;
        options = args;
    } else if (typeof command === 'string') {
        if (Array.isArray(args)) {
            cmdArgs = [command, ...args];
        } else if (args && typeof args === 'object') {
            options = args;
            cmdArgs = [command];
        } else if (args == null) {
            cmdArgs = [command];
        } else {
            throw new TypeError('The "args" argument must be of type array');
        }
    } else {
        throw new TypeError('The "command" argument must be of type string or array');
    }

    // Copy and adjust options for internal processing.
    // Node's child_process.spawn defaults stdio to 'pipe'.
    const opts = { ...options };
    let stdinWrapper, stdoutWrapper, stderrWrapper;

    // Handle stdio options - translate nodejs style to tjs style for stream creation
    // If stdio is array format like ['pipe', 'pipe', 'ignore'], map it appropriately
    if (Array.isArray(opts.stdio)) {
        if (opts.stdin === undefined) {
            opts.stdin = opts.stdio[0];
        }
        if (opts.stdout === undefined) {
            opts.stdout = opts.stdio[1];
        }
        if (opts.stderr === undefined) {
            opts.stderr = opts.stdio[2];
        }
        delete opts.stdio;
    }

    if (opts.stdin === undefined) {
        opts.stdin = 'pipe';
    }
    if (opts.stdout === undefined) {
        opts.stdout = 'pipe';
    }
    if (opts.stderr === undefined) {
        opts.stderr = 'pipe';
    }

    // Create stream handles if needed
    if (opts.stdin === 'pipe') {
        const handle = new core.Pipe();
        opts.stdin = handle;
        stdinWrapper = new ProcessWritableStream(handle);
    }

    if (opts.stdout === 'pipe') {
        const handle = new core.Pipe();
        opts.stdout = handle;
        stdoutWrapper = new ProcessReadableStream(handle);
    }

    if (opts.stderr === 'pipe') {
        const handle = new core.Pipe();
        opts.stderr = handle;
        stderrWrapper = new ProcessReadableStream(handle);
    }

    try {
        const proc = core.spawn(cmdArgs, opts);

        // Return subprocess with streams mapped like in tjs
        return new Subprocess(proc, stdinWrapper, stdoutWrapper, stderrWrapper);
    } catch (error) {
        // Node.js behavior: spawn() does not throw for ENOENT; child emits error/close.
        const child = new Subprocess(null, stdinWrapper, stdoutWrapper, stderrWrapper);
        stdinWrapper?._closeAfterExit?.();
        stdoutWrapper?._closeAfterExit?.();
        stderrWrapper?._closeAfterExit?.();
        process.nextTick(() => {
            child.emit('error', error);
            child.emit('close', -2, null);
        });
        return child;
    }
}

export function spawnSync(command, args, options) {
    let cmdArgs;
    let opts = options;

    if (Array.isArray(command)) {
        cmdArgs = command;
        opts = args;
    } else if (typeof command === 'string') {
        if (Array.isArray(args)) {
            cmdArgs = [ command, ...args ];
        } else if (args && typeof args === 'object') {
            opts = args;
            cmdArgs = [ command ];
        } else if (args == null) {
            cmdArgs = [ command ];
        } else {
            throw new TypeError('The "args" argument must be of type array');
        }
    } else {
        throw new TypeError('The "command" argument must be of type string or array');
    }

    if (cmdArgs.length === 0 || typeof cmdArgs[0] !== 'string' || cmdArgs[0].length === 0) {
        throw new TypeError('spawnSync command must not be empty');
    }

    const normalizedOptions = opts == null ? {} : (typeof opts === 'object' ? opts : {});
    const encoding = normalizedOptions.encoding;

    const shellEscape = (value) => `'${String(value).replace(/'/g, `'\\''`)}'`;
    const commandLine = cmdArgs.map(shellEscape).join(' ');
    let wrappedCommand = commandLine;

    if (normalizedOptions.cwd) {
        wrappedCommand = `cd ${shellEscape(normalizedOptions.cwd)} && ${wrappedCommand}`;
    }

    if (normalizedOptions.env && typeof normalizedOptions.env === 'object') {
        const envPrefix = Object.entries(normalizedOptions.env)
            .map(([ key, value ]) => `${key}=${shellEscape(value ?? '')}`)
            .join(' ');
        if (envPrefix.length > 0) {
            wrappedCommand = `${envPrefix} ${wrappedCommand}`;
        }
    }

    const result = core.execSync(wrappedCommand, normalizedOptions);
    const toOutput = value => {
        const buf = typeof value === 'string' ? Buffer.from(value) : Buffer.from(value ?? '');
        if (encoding == null || encoding === 'buffer') {
            return buf;
        }
        if (encoding === 'utf8' || encoding === 'utf-8') {
            return buf.toString('utf8');
        }
        throw new TypeError(`Unsupported encoding: ${encoding}`);
    };

    const stdout = toOutput(result?.stdout);
    const stderr = toOutput(result?.stderr);
    const status = result?.status ?? null;
    const signal = result?.signal ?? null;
    const ret = {
        status,
        signal,
        output: [ null, stdout, stderr ],
        pid: result?.pid,
        stdout,
        stderr,
    };

    // Keep legacy fields used by some existing tjs code.
    ret.exit_status = status;
    ret.term_signal = signal;

    return ret;
}

export function exec(command, options, callback) {
    if (typeof options === 'function') {
        callback = options;
        options = {};
    }

    const opts = options && typeof options === 'object' ? { ...options } : {};
    const encoding = opts.encoding ?? 'utf8';
    const stdoutChunks = [];
    const stderrChunks = [];
    let spawnError = null;

    // exec should collect stdout/stderr by default.
    opts.stdio = [ 'ignore', 'pipe', 'pipe' ];
    if (opts.stdout === undefined) opts.stdout = 'pipe';
    if (opts.stderr === undefined) opts.stderr = 'pipe';

    const child = spawn('/bin/sh', [ '-c', command ], opts);

    child.stdout?.on?.('data', chunk => {
        stdoutChunks.push(Buffer.isBuffer(chunk) ? chunk : Buffer.from(chunk));
    });
    child.stderr?.on?.('data', chunk => {
        stderrChunks.push(Buffer.isBuffer(chunk) ? chunk : Buffer.from(chunk));
    });
    child.on('error', error => {
        spawnError = error;
    });

    child.on('close', (code, signal) => {
        const stdoutBuffer = Buffer.concat(stdoutChunks);
        const stderrBuffer = Buffer.concat(stderrChunks);
        const stdout = encoding === 'buffer' ? stdoutBuffer : stdoutBuffer.toString(encoding);
        const stderr = encoding === 'buffer' ? stderrBuffer : stderrBuffer.toString(encoding);

        let error = spawnError;
        if (!error && (code !== 0 || signal)) {
            error = new Error(`Command failed: ${command}`);
            error.code = code;
            error.signal = signal;
            error.killed = signal != null;
            error.cmd = command;
        }

        if (callback) {
            callback(error, stdout, stderr);
        }
    });

    return child;
}

function normalizeExecSyncOptions(options) {
    if (options == null) {
        return {};
    }

    if (typeof options === 'string') {
        return { encoding: options };
    }

    if (typeof options !== 'object') {
        throw new TypeError('The "options" argument must be of type object or string');
    }

    return options;
}

function toExecSyncOutput(stdout, encoding) {
    const out = typeof stdout === 'string' ? Buffer.from(stdout) : Buffer.from(stdout ?? '');
    if (encoding == null || encoding === 'buffer') {
        return out;
    }

    if (encoding === 'utf8' || encoding === 'utf-8') {
        return out.toString('utf8');
    }

    throw new TypeError(`Unsupported encoding: ${encoding}`);
}

export function execSync(command, options) {
    const normalizedOptions = normalizeExecSyncOptions(options);
    const result = core.execSync(command, normalizedOptions);

    const status = result?.status;
    if (status !== 0 || result?.signal) {
        const stderrText = typeof result?.stderr === 'string' ? result.stderr : Buffer.from(result?.stderr ?? '').toString('utf8');
        const message = stderrText
            ? `Command failed: ${command}\n${stderrText}`
            : `Command failed: ${command}`;
        const error = new Error(message);
        error.status = status;
        error.signal = result?.signal ?? null;
        error.stdout = Buffer.from(result?.stdout ?? '');
        error.stderr = Buffer.from(result?.stderr ?? '');
        error.output = [ null, error.stdout, error.stderr ];
        error.pid = result?.pid;
        throw error;
    }

    return toExecSyncOutput(result?.stdout, normalizedOptions.encoding);
}

export const ChildProcess = core.ChildProcess || null;
