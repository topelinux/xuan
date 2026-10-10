const core = globalThis[Symbol.for('tjs.internal.core')];
const EventEmitter = core.events?.EventEmitter;
const textEncoder = new TextEncoder();

function mapArch(machine = '') {
    const value = machine.toLowerCase();
    if (value === 'x86_64' || value === 'amd64') return 'x64';
    if (value === 'aarch64' || value === 'arm64') return 'arm64';
    if (value.startsWith('arm')) return 'arm';
    return value || 'unknown';
}

function makeHrtime() {
    const ms = performance.now();
    const sec = Math.floor(ms / 1000);
    const ns = Math.floor((ms - sec * 1000) * 1e6);
    return [ sec, ns ];
}

function hrtime(time) {
    const now = makeHrtime();
    if (!time) {
        return now;
    }
    let sec = now[0] - time[0];
    let ns = now[1] - time[1];
    if (ns < 0) {
        sec -= 1;
        ns += 1e9;
    }
    return [ sec, ns ];
}

hrtime.bigint = () => {
    const [ sec, ns ] = makeHrtime();
    return BigInt(sec) * 1000000000n + BigInt(ns);
};

const emitter = EventEmitter ? new EventEmitter() : {
    on() { return this; },
    off() { return this; },
    emit() { return false; }
};

function toBytes(data, encoding = 'utf8') {
    if (data instanceof Uint8Array) return data;
    if (ArrayBuffer.isView(data)) {
        return new Uint8Array(data.buffer, data.byteOffset, data.byteLength);
    }
    if (data instanceof ArrayBuffer) return new Uint8Array(data);
    if (typeof data === 'string') {
        if (encoding && encoding !== 'utf8' && encoding !== 'utf-8') {
            throw new TypeError(`Unsupported encoding: ${encoding}`);
        }
        return textEncoder.encode(data);
    }
    return textEncoder.encode(String(data));
}

function createNodeStdStream(stream, fd) {
    const writer = stream.getWriter();
    return {
        fd,
        isTTY: stream.isTerminal === true,
        write(chunk, encoding, callback) {
            if (typeof encoding === "function") {
                callback = encoding;
                encoding = undefined;
            }
            const bytes = toBytes(chunk, encoding);
            writer.write(bytes).then(
                () => { if (callback) callback(null); },
                (err) => { if (callback) callback(err); }
            );
            return true;
        }
    };
}

const processObj = Object.assign(emitter, {
    argv: Array.from(tjs.args),
    argv0: tjs.args[0] ?? '',
    env: tjs.env,
    title: 'tjs',
    version: `v${tjs.version}`,
    versions: tjs.engine.versions,
    platform: core.platform,
    arch: mapArch(core.uname().machine),
    execPath: tjs.exePath,
    nextTick(callback, ...args) {
        if (typeof callback !== 'function') {
            throw new TypeError('callback must be a function');
        }
        queueMicrotask(() => callback(...args));
    },
    cwd() {
        return tjs.cwd;
    },
    chdir(dir) {
        return tjs.chdir(dir);
    },
    exit(code = 0) {
        return tjs.exit(code);
    },
    kill(pid, signal) {
        return tjs.kill(pid, signal);
    },
    hrtime,
    uptime() {
        return performance.now() / 1000;
    },
    emitWarning(warning, type = 'Warning', code = undefined) {
        const msg = warning instanceof Error ? warning.message : String(warning);
        const prefix = code ? `[${code}] ` : '';
        console.warn(`${type}: ${prefix}${msg}`);
        this.emit('warning', warning);
    }
});

Object.defineProperties(processObj, {
    pid: { enumerable: true, get: () => tjs.pid },
    ppid: { enumerable: true, get: () => tjs.ppid },
    stdin: { enumerable: true, get: () => tjs.stdin },
    stdout: { enumerable: true, get: (() => {
        let cached;
        return () => {
            if (!cached) cached = createNodeStdStream(tjs.stdout, 1);
            return cached;
        };
    })() },
    stderr: { enumerable: true, get: (() => {
        let cached;
        return () => {
            if (!cached) cached = createNodeStdStream(tjs.stderr, 2);
            return cached;
        };
    })() },
});

if (!globalThis.process) {
    Object.defineProperty(globalThis, 'process', {
        value: processObj,
        writable: true,
        configurable: true,
        enumerable: false
    });
}

export default processObj;
