const core = globalThis[Symbol.for('tjs.internal.core')];
import { Readable, Writable } from 'tjs:stream';
import { join as joinPath } from 'tjs:path';

const textEncoder = new TextEncoder();

class ReadStream extends Readable {
    constructor(path, options = undefined) {
        const opts = typeof options === 'string'
            ? { encoding: options }
            : (options ?? {});
        super({ highWaterMark: opts.highWaterMark ?? 64 * 1024 });
        this.path = path;
        this.flags = opts.flags ?? 'r';
        this.mode = opts.mode ?? 0o666;
        this.autoClose = opts.autoClose !== false;
        this.start = opts.start;
        this.end = opts.end;
        this.pos = this.start ?? 0;
        this.bytesRead = 0;
        this.fd = null;
        this._closed = false;

        if (this.start != null && this.start < 0) {
            throw new RangeError('"start" must be >= 0');
        }
        if (this.end != null && this.end < 0) {
            throw new RangeError('"end" must be >= 0');
        }
        if (this.start != null && this.end != null && this.start > this.end) {
            throw new RangeError('"start" must be <= "end"');
        }
        if (opts.encoding != null) {
            this.setEncoding(opts.encoding);
        }

        queueMicrotask(() => {
            this._pump().catch(err => {
                this.emit('error', err);
                if (this.autoClose) {
                    void this._closeFd();
                }
            });
        });
    }

    async _pump() {
        this.fd = core.openSync(this.path, this.flags, this.mode);
        this.emit('open', this.fd);

        try {
            while (!this._destroyed) {
                let readSize = this._highWaterMark;
                if (this.end != null) {
                    if (this.pos > this.end) {
                        break;
                    }
                    readSize = Math.min(readSize, this.end - this.pos + 1);
                }

                const chunk = new Uint8Array(readSize);
                const bytes = core.readSync(this.fd, chunk, 0, readSize, this.pos);
                if (!bytes) {
                    break;
                }

                this.pos += bytes;
                this.bytesRead += bytes;
                this.push(bytes === chunk.byteLength ? chunk : chunk.subarray(0, bytes));
            }

            if (!this._destroyed) {
                this.push(null);
            }
        } finally {
            if (this.autoClose) {
                await this._closeFd();
            }
        }
    }

    _closeFd() {
        if (this._closed || this.fd == null) {
            return;
        }

        const fd = this.fd;
        this.fd = null;
        core.closeSync(fd);
        this._closed = true;
        this.emit('close');
    }

    close(callback) {
        try {
            this._closeFd();
            if (typeof callback === 'function') callback(null);
        } catch (err) {
            if (typeof callback === 'function') callback(err);
            else this.emit('error', err);
        }
        return this;
    }

    destroy(err) {
        if (this._destroyed) return this;
        this._destroyed = true;
        if (err) this.emit('error', err);
        if (this.autoClose) {
            void this._closeFd();
        } else {
            this.emit('close');
        }
        return this;
    }
}

function createReadStream(path, options = undefined) {
    return new ReadStream(path, options);
}

class WriteStream extends Writable {
    constructor(path, options = undefined) {
        const opts = typeof options === 'string'
            ? { encoding: options }
            : (options ?? {});
        super({ highWaterMark: opts.highWaterMark ?? 16 * 1024 });
        this.path = path;
        this.flags = opts.flags ?? 'w';
        this.mode = opts.mode ?? 0o666;
        this.autoClose = opts.autoClose !== false;
        this.start = opts.start;
        this.pos = this.start ?? 0;
        this.bytesWritten = 0;
        this.fd = null;
        this._closed = false;
        this._pendingWrite = null;

        if (this.start != null && this.start < 0) {
            throw new RangeError('"start" must be >= 0');
        }

        queueMicrotask(() => {
            this._open().catch(err => {
                this.emit('error', err);
            });
        });
    }

    async _open() {
        try {
            this.fd = core.openSync(this.path, this.flags, this.mode);
            this.emit('open', this.fd);
        } catch (err) {
            this.emit('error', err);
        }
    }

    _write(chunk, encoding, callback) {
        if (this.fd == null) {
            // Wait for open to complete
            this.once('open', () => {
                this._doWrite(chunk, callback);
            });
            return;
        }
        this._doWrite(chunk, callback);
    }

    _doWrite(chunk, callback) {
        try {
            const bytes = core.writeSync(this.fd, chunk, 0, chunk.byteLength, this.pos);
            this.pos += bytes;
            this.bytesWritten += bytes;
            callback(null);
        } catch (err) {
            callback(err);
        }
    }

    _final(callback) {
        if (this.autoClose) {
            this._closeFd();
        }
        // Don't call callback here - let base class handle finish/close timing
        if (callback) callback(null);
    }

    _closeFd() {
        if (this._closed || this.fd == null) {
            return;
        }

        const fd = this.fd;
        this.fd = null;
        try {
            core.closeSync(fd);
        } catch (err) {
            this.emit('error', err);
        }
        this._closed = true;
        // Don't emit 'close' here - let the base Writable class handle it
        // in end() after 'finish', or destroy() for error paths.
    }

    close(callback) {
        try {
            this._closeFd();
            if (typeof callback === 'function') callback(null);
        } catch (err) {
            if (typeof callback === 'function') callback(err);
            else this.emit('error', err);
        }
        return this;
    }

    destroy(err) {
        if (this._destroyed) return this;
        this._destroyed = true;
        if (err) this.emit('error', err);
        if (this.autoClose) {
            this._closeFd();
        }
        this.emit('close');
        return this;
    }
}

function createWriteStream(path, options = undefined) {
    return new WriteStream(path, options);
}

function asBuffer(data, encoding = 'utf8') {
    if (data instanceof Uint8Array) {
        return data;
    }

    if (typeof data === 'string') {
        if (encoding !== 'utf8' && encoding !== 'utf-8') {
            throw new TypeError(`Unsupported encoding: ${encoding}`);
        }

        return textEncoder.encode(data);
    }

    if (ArrayBuffer.isView(data)) {
        return new Uint8Array(data.buffer, data.byteOffset, data.byteLength);
    }

    if (data instanceof ArrayBuffer) {
        return new Uint8Array(data);
    }

    throw new TypeError('data must be string, Buffer, TypedArray, or ArrayBuffer');
}

function parseEncoding(options) {
    if (!options) {
        return null;
    }

    if (typeof options === 'string') {
        return options;
    }

    return options.encoding ?? null;
}

function parseWriteOptions(options, defaults) {
    if (!options) {
        return defaults;
    }

    if (typeof options === 'string') {
        return { ...defaults, encoding: options };
    }

    return { ...defaults, ...options };
}

function maybeCallback(promise, callback) {
    if (typeof callback !== 'function') {
        return promise;
    }

    promise.then(
        value => callback(null, value),
        err => callback(err)
    );

    return undefined;
}

function maybeWriteCallback(promise, callback, chunk) {
    if (typeof callback !== 'function') {
        return promise;
    }

    promise.then(
        value => callback(null, value, chunk),
        err => callback(err)
    );

    return undefined;
}

function toStats(stats) {
    if (!stats || typeof stats !== 'object') {
        return stats;
    }
    if (typeof stats.isDirectory === 'function') {
        return stats;
    }

    const wrapped = { ...stats };
    const timeAliases = [
        [ 'atime', 'atim' ],
        [ 'mtime', 'mtim' ],
        [ 'ctime', 'ctim' ],
        [ 'birthtime', 'birthtim' ],
    ];

    for (const [ target, source ] of timeAliases) {
        if (wrapped[target] == null && wrapped[source] != null) {
            wrapped[target] = wrapped[source];
        }
        if (wrapped[`${target}Ms`] == null) {
            const value = wrapped[target];
            wrapped[`${target}Ms`] = value instanceof Date ? value.getTime() : Number(value ?? 0);
        }
    }

    const typeFlags = [
        'isFile',
        'isDirectory',
        'isSymbolicLink',
        'isFIFO',
        'isSocket',
        'isCharacterDevice',
        'isBlockDevice',
    ];

    for (const key of typeFlags) {
        const value = Boolean(stats[key]);
        wrapped[key] = () => value;
    }

    return wrapped;
}

async function readFileImpl(path, options = undefined) {
    const data = await core.readFile(path);
    const encoding = parseEncoding(options);

    if (!encoding) {
    // Node.js returns a Buffer by default when no encoding is specified.
        return globalThis.Buffer ? globalThis.Buffer.from(data) : data;
    }

    if (encoding !== 'utf8' && encoding !== 'utf-8') {
        throw new TypeError(`Unsupported encoding: ${encoding}`);
    }

    return new TextDecoder('utf-8').decode(data);
}

async function writeFileImpl(path, data, options = undefined) {
    const opts = parseWriteOptions(options, { encoding: 'utf8', mode: 0o666, flag: 'w' });
    const bytes = asBuffer(data, opts.encoding);
    const handle = await core.open(path, opts.flag, opts.mode);

    try {
        let offset = 0;

        while (offset < bytes.length) {
            const written = await handle.write(bytes.subarray(offset));

            if (!written) {
                break;
            }

            offset += written;
        }
    } finally {
        await handle.close();
    }
}

async function appendFileImpl(path, data, options = undefined) {
    const opts = parseWriteOptions(options, { encoding: 'utf8', mode: 0o666, flag: 'a' });

    return writeFileImpl(path, data, opts);
}

async function readdirImpl(path, options = undefined) {
    const withFileTypes = typeof options === 'object' && options?.withFileTypes;
    const out = [];
    const it = await core.readDir(path);

    for await (const entry of it) {
        out.push(withFileTypes ? entry : entry.name);
    }

    return out;
}

function readFile(path, options, callback) {
    if (typeof options === 'function') {
        callback = options;
        options = undefined;
    }

    return maybeCallback(readFileImpl(path, options), callback);
}

function writeFile(path, data, options, callback) {
    if (typeof options === 'function') {
        callback = options;
        options = undefined;
    }

    return maybeCallback(writeFileImpl(path, data, options), callback);
}

function appendFile(path, data, options, callback) {
    if (typeof options === 'function') {
        callback = options;
        options = undefined;
    }

    return maybeCallback(appendFileImpl(path, data, options), callback);
}

function readdir(path, options, callback) {
    if (typeof options === 'function') {
        callback = options;
        options = undefined;
    }

    return maybeCallback(readdirImpl(path, options), callback);
}

function readdirSync(path, options = undefined) {
    const withFileTypes = typeof options === 'object' && options?.withFileTypes;
    const entries = core.readDirSync(path);

    if (withFileTypes) {
        return entries;
    }

    const out = [];
    for (const entry of entries) {
        out.push(entry.name);
    }

    return out;
}

function stat(path, options, callback) {
    if (typeof options === 'function') {
        callback = options;
    }

    return maybeCallback(core.stat(path).then(toStats), callback);
}

function lstat(path, options, callback) {
    if (typeof options === 'function') {
        callback = options;
    }

    return maybeCallback(core.lstat(path).then(toStats), callback);
}

function unlink(path, callback) {
    return maybeCallback(core.unlink(path), callback);
}

function mkdir(path, options, callback) {
    if (typeof options === 'function') {
        callback = options;
        options = undefined;
    }

    const mode = typeof options === 'object' ? options.mode : options;

    return maybeCallback(core.mkdir(path, mode ?? 0o777), callback);
}

function rmdir(path, callback) {
    return maybeCallback(core.rmdir(path), callback);
}

function rename(oldPath, newPath, callback) {
    return maybeCallback(core.rename(oldPath, newPath), callback);
}

function copyFile(src, dest, callback) {
    return maybeCallback(core.copyFile(src, dest), callback);
}

function realpath(path, callback) {
    return maybeCallback(core.realPath(path), callback);
}

function readlink(path, callback) {
    return maybeCallback(core.readLink(path), callback);
}

function symlink(target, path, type, callback) {
    if (typeof type === 'function') {
        callback = type;
        type = undefined;
    }

    let flags = 0;

    if (type === 'dir' || type === 'directory') {
        flags = core.FS_SYMLINK_DIR;
    } else if (type === 'junction') {
        flags = core.FS_SYMLINK_JUNCTION;
    }

    return maybeCallback(core.symlink(target, path, flags), callback);
}

function link(existingPath, newPath, callback) {
    return maybeCallback(core.link(existingPath, newPath), callback);
}

function chmod(path, mode, callback) {
    return maybeCallback(core.chmod(path, mode), callback);
}

function exists(path, callback) {
    if (typeof callback !== 'function') {
        throw new TypeError('callback must be a function');
    }

    core.stat(path).then(
        () => callback(true),
        () => callback(false)
    );
}

function existsSync(path) {
    return core.existsSync(path);
}

function statSync(path) {
    return toStats(core.statSync(path));
}

function lstatSync(path) {
    return toStats(core.statSync(path, true));
}

function mkdirSync(path, options = undefined) {
    const opts = typeof options === 'object' ? options : { mode: options };
    const mode = opts.mode ?? 0o777;
    const recursive = opts.recursive ?? false;

    if (!recursive) {
        return core.mkdirSync(path, mode);
    }

    const pathParts = path.split('/');
    let currentPath = '';

    for (let i = 0; i < pathParts.length; i++) {
        const part = pathParts[i];
        if (!part) {
            if (i === 0) {
                currentPath = '/';
            }
            continue;
        }

        currentPath = currentPath === '/' ? `/${part}` : `${currentPath}/${part}`;

        if (!core.existsSync(currentPath)) {
            core.mkdirSync(currentPath, mode);
        }
    }

    return undefined;
}

function readFileSync(path, options = undefined) {
    const data = core.readFileSync(path);
    const encoding = parseEncoding(options);

    if (!encoding) {
    // Node.js returns a Buffer by default when no encoding is specified.
        return globalThis.Buffer ? globalThis.Buffer.from(data) : data;
    }

    if (encoding !== 'utf8' && encoding !== 'utf-8') {
        throw new TypeError(`Unsupported encoding: ${encoding}`);
    }

    return new TextDecoder('utf-8').decode(data);
}

function writeFileSync(path, data, options = undefined) {
    const opts = parseWriteOptions(options, { encoding: 'utf8', mode: 0o666, flag: 'w' });
    const bytes = asBuffer(data, opts.encoding);
    if (typeof path === 'number') {
        let offset = 0;
        while (offset < bytes.length) {
            const n = core.writeSync(path, bytes, offset, bytes.length - offset, undefined);
            if (!n) throw new Error('Unable to write file descriptor');
            offset += n;
        }
        return;
    }
    return core.writeFileSync(path, bytes, opts.flag, opts.mode);
}

function appendFileSync(path, data, options = undefined) {
    const opts = parseWriteOptions(options, { encoding: 'utf8', mode: 0o666, flag: 'a' });
    const bytes = asBuffer(data, opts.encoding);

    return core.writeFileSync(path, bytes, opts.flag, opts.mode);
}

function unlinkSync(path) {
    return core.unlinkSync(path);
}

function renameSync(oldPath, newPath) {
    return core.renameSync(oldPath, newPath);
}

function rmdirSync(path) {
    return core.rmdirSync(path);
}

function rmSync(path, options = undefined) {
    const force = typeof options === 'object' && !!options?.force;
    const recursive = typeof options === 'object' && !!options?.recursive;

    if (recursive) {
        if (!existsSync(path)) {
            if (force) return;

            return core.rmSync(path, false);
        }

        if (lstatSync(path).isDirectory()) {
            for (const name of readdirSync(path)) {
                rmSync(joinPath(String(path), name), options);
            }
        }
    }

    return core.rmSync(path, force);
}

function closeSync(fd) {
    return core.closeSync(fd);
}
function fsyncSync(fd) { return core.fsyncSync(fd); }
function chmodSync(path, mode) { return core.chmodSync(path, mode); }
function mkdtempSync(prefix) { return core.mkdtempSync(prefix); }

function openSync(path, flags = 'r', mode = 0o666) {
    return core.openSync(path, flags, mode);
}

function readSync(fd, buffer, offset = 0, length = undefined, position = undefined) {
    const bytes = asBuffer(buffer);

    if (length === undefined) {
        length = bytes.byteLength - offset;
    }

    return core.readSync(fd, bytes, offset, length, position);
}

function write(fd, data, offset, length, position, callback) {
    let cb = callback;

    if (typeof data === 'string') {
        let pos = offset;
        let encoding = 'utf8';

        if (typeof offset === 'function') {
            cb = offset;
            pos = undefined;
        } else if (typeof length === 'function') {
            cb = length;
            if (typeof offset === 'string') {
                encoding = offset;
                pos = undefined;
            } else if (typeof position === 'string') {
                encoding = position;
            }
        } else if (typeof position === 'function') {
            cb = position;
            if (typeof length === 'string') {
                encoding = length;
            }
        }

        const bytes = asBuffer(data, encoding);
        return maybeWriteCallback(
            core.write(fd, bytes, 0, bytes.byteLength, pos),
            cb,
            data
        );
    }

    if (typeof offset === 'function') {
        cb = offset;
        offset = 0;
        length = undefined;
        position = undefined;
    } else if (typeof length === 'function') {
        cb = length;
        length = undefined;
        position = undefined;
    } else if (typeof position === 'function') {
        cb = position;
        position = undefined;
    }

    const bytes = asBuffer(data);
    const start = offset ?? 0;
    const size = length ?? (bytes.byteLength - start);
    return maybeWriteCallback(
        core.write(fd, bytes, start, size, position),
        cb,
        data
    );
}

function writeSync(fd, buffer, offset = 0, length = undefined, position = undefined) {
    const bytes = asBuffer(buffer);

    if (length === undefined) {
        length = bytes.byteLength - offset;
    }

    return core.writeSync(fd, bytes, offset, length, position);
}

const promises = {
    readFile: readFileImpl,
    writeFile: writeFileImpl,
    appendFile: appendFileImpl,
    readdir: readdirImpl,
    stat: (path, _options) => core.stat(path).then(toStats),
    lstat: (path, _options) => core.lstat(path).then(toStats),
    unlink: path => core.unlink(path),
    mkdir: (path, options) => mkdir(path, options),
    rmdir: path => core.rmdir(path),
    rename: (oldPath, newPath) => core.rename(oldPath, newPath),
    copyFile: (src, dest) => core.copyFile(src, dest),
    realpath: path => core.realPath(path),
    readlink: path => core.readLink(path),
    symlink: (target, path, type) => symlink(target, path, type),
    link: (existingPath, newPath) => core.link(existingPath, newPath),
    chmod: (path, mode) => core.chmod(path, mode),
    open: (path, flags, mode) => core.open(path, flags, mode),
};

const fs = {
    fsyncSync, chmodSync, mkdtempSync,
    promises,
    readFile,
    writeFile,
    appendFile,
    readdir,
    readdirSync,
    stat,
    lstat,
    unlink,
    mkdir,
    rmdir,
    rename,
    copyFile,
    realpath,
    readlink,
    symlink,
    link,
    chmod,
    exists,
    existsSync,
    readFileSync,
    statSync,
    lstatSync,
    mkdirSync,
    writeFileSync,
    appendFileSync,
    unlinkSync,
    renameSync,
    rmdirSync,
    rmSync,
    closeSync,
    openSync,
    readSync,
    write,
    writeSync,
    ReadStream,
    createReadStream,
    WriteStream,
    createWriteStream,
    open: (path, flags, mode, callback) => maybeCallback(core.open(path, flags, mode), callback),
    constants: {
        UV_FS_SYMLINK_DIR: core.FS_SYMLINK_DIR,
        UV_FS_SYMLINK_JUNCTION: core.FS_SYMLINK_JUNCTION,
    }
};

export {
    fsyncSync, chmodSync, mkdtempSync,
    promises,
    readFile,
    writeFile,
    appendFile,
    readdir,
    readdirSync,
    stat,
    lstat,
    unlink,
    mkdir,
    rmdir,
    rename,
    copyFile,
    realpath,
    readlink,
    symlink,
    link,
    chmod,
    exists,
    existsSync,
    readFileSync,
    statSync,
    lstatSync,
    mkdirSync,
    writeFileSync,
    appendFileSync,
    unlinkSync,
    renameSync,
    rmdirSync,
    rmSync,
    closeSync,
    openSync,
    readSync,
    write,
    writeSync,
    ReadStream,
    createReadStream,
    WriteStream,
    createWriteStream
};

export default fs;
