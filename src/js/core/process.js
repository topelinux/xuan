const core = globalThis[Symbol.for('tjs.internal.core')];

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

function stopRead(state) {
    if (!state.reading) {
        return;
    }

    try {
        state.handle.stopRead();
    } catch {
        // Ignored.
    }
    state.reading = false;
}

function closeReadableState(state) {
    if (!state || state.closed) {
        return;
    }

    stopRead(state);
    state.closed = true;
    state.handle.onread = null;
    silentClose(state.handle);
}

function closeWritableState(state) {
    if (!state || state.closed) {
        return;
    }

    state.closed = true;
    silentClose(state.handle);
}

async function readAllChunks(stream) {
    const reader = stream.getReader();
    const chunks = [];

    while (true) {
        const { done, value } = await reader.read();

        if (done) {
            break;
        }

        chunks.push(value);
    }

    let totalLength = 0;

    for (const chunk of chunks) {
        totalLength += chunk.byteLength;
    }

    const result = new Uint8Array(totalLength);
    let offset = 0;

    for (const chunk of chunks) {
        result.set(chunk, offset);
        offset += chunk.byteLength;
    }

    return result;
}

class ProcessReadableStream extends ReadableStream {
    constructor(handle) {
        const state = {
            closed: false,
            controller: null,
            handle,
            reading: false
        };

        super({
            start(controller) {
                state.controller = controller;
                handle.onread = (data, error) => {
                    if (error) {
                        stopRead(state);
                        state.closed = true;
                        handle.onread = null;
                        controller.error(error);
                        silentClose(handle);
                    } else if (data === null) {
                        stopRead(state);
                        state.closed = true;
                        handle.onread = null;
                        controller.close();
                        silentClose(handle);
                    } else {
                        controller.enqueue(data);

                        if (controller.desiredSize <= 0) {
                            stopRead(state);
                        }
                    }
                };
            },
            pull(controller) {
                if (state.closed) {
                    try {
                        controller.close();
                    } catch {
                        // Ignored.
                    }
                    return;
                }

                if (!state.reading) {
                    state.reading = true;
                    try {
                        handle.startRead();
                    } catch (e) {
                        state.reading = false;
                        state.closed = true;
                        handle.onread = null;
                        silentClose(handle);
                        controller.close();
                    }
                }
            },
            cancel() {
                closeReadableState(state);
            }
        });

        this._closeAfterExit = () => {
            if (state.reading) {
                try {
                    handle.stopRead();
                } catch {
                    // Ignored.
                }
                state.reading = false;
            }
            silentClose(handle);
        };
    }

    arrayBuffer() {
        return readAllChunks(this).then(bytes => bytes.buffer);
    }

    bytes() {
        return readAllChunks(this);
    }

    text() {
        return readAllChunks(this).then(bytes => new TextDecoder().decode(bytes));
    }
}

class ProcessWritableStream extends WritableStream {
    constructor(handle) {
        const queue = [];
        const state = {
            closed: false,
            handle
        };

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

        super({
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
            close() {
                closeWritableState(state);
            },
            abort() {
                closeWritableState(state);
            }
        });

        this._closeAfterExit = () => {
            closeWritableState(state);
        };
    }
}

class Subprocess {
    constructor(proc, stdin, stdout, stderr) {
        this[kProcess] = proc;
        this[kStdin] = stdin ?? null;
        this[kStdout] = stdout ?? null;
        this[kStderr] = stderr ?? null;
        this._waitPromise = proc.wait().then(status => {
            this._closeStdioAfterExit();
            return status;
        }, error => {
            this._closeStdioAfterExit();
            throw error;
        });
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
        return this[kProcess].kill(sig);
    }

    wait() {
        return this._waitPromise;
    }

    _closeStdioAfterExit() {
        this[kStdin]?._closeAfterExit?.();
        this[kStdout]?._closeAfterExit?.();
        this[kStderr]?._closeAfterExit?.();
    }
}

export function spawn(args, options) {
    const opts = { ...options };
    let stdin, stdout, stderr;

    if (opts.stdin === 'pipe') {
        const handle = new core.Pipe();

        opts.stdin = handle;
        stdin = new ProcessWritableStream(handle);
    }

    if (opts.stdout === 'pipe') {
        const handle = new core.Pipe();

        opts.stdout = handle;
        stdout = new ProcessReadableStream(handle);
    }

    if (opts.stderr === 'pipe') {
        const handle = new core.Pipe();

        opts.stderr = handle;
        stderr = new ProcessReadableStream(handle);
    }

    try {
        const proc = core.spawn(args, opts);

        return new Subprocess(proc, stdin, stdout, stderr);
    } catch (error) {
        stdin?._closeAfterExit?.();
        stdout?._closeAfterExit?.();
        stderr?._closeAfterExit?.();
        throw error;
    }
}

export function spawnSync(args, options) {
    return core.spawnSync(args, options);
}

export function execSync(command, options) {
    return core.execSync(command, options);
}
