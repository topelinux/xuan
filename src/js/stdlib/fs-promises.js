const core = globalThis[Symbol.for('tjs.internal.core')];

async function readFile(path, options) {
    const data = await core.readFile(path);
    if (!options || (!options.encoding && !options.flag)) {
        return globalThis.Buffer ? globalThis.Buffer.from(data) : data;
    }
    if (options.encoding === 'utf8' || options.encoding === 'utf-8') {
        return new TextDecoder('utf-8').decode(data);
    }
    throw new TypeError(`Unsupported encoding: ${options.encoding}`);
}

async function writeFile(path, data, options) {
    const encoding = typeof options === 'object' ? options.encoding : options;
    const mode = typeof options === 'object' ? options.mode : 0o666;
    const flag = typeof options === 'object' ? options.flag : 'w';

    if (typeof data === 'string') {
        data = new TextEncoder().encode(data);
    } else if (data instanceof Uint8Array) {
        // ok
    } else if (ArrayBuffer.isView(data)) {
        data = new Uint8Array(data.buffer, data.byteOffset, data.byteLength);
    } else if (data instanceof ArrayBuffer) {
        data = new Uint8Array(data);
    } else {
        throw new TypeError('data must be string, Buffer, TypedArray, or ArrayBuffer');
    }

    const handle = await core.open(path, flag, mode ?? 0o666);
    try {
        let offset = 0;
        while (offset < data.length) {
            const written = await handle.write(data.subarray(offset));
            if (!written) {
                break;
            }
            offset += written;
        }
    } finally {
        await handle.close();
    }
}

async function appendFile(path, data, options) {
    const opts = typeof options === 'object' ? { ...options, flag: 'a' } : { encoding: options, flag: 'a' };
    return writeFile(path, data, opts);
}

async function readdir(path, options) {
    const withFileTypes = typeof options === 'object' && options.withFileTypes;
    const out = [];
    const it = await core.readDir(path);
    for await (const entry of it) {
        out.push(withFileTypes ? entry : entry.name);
    }
    return out;
}

async function stat(path, options) {
    const stats = await core.stat(path);
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

    const typeFlags = [ 'isFile', 'isDirectory', 'isSymbolicLink' ];
    for (const key of typeFlags) {
        wrapped[key] = () => Boolean(stats[key]);
    }

    return wrapped;
}

async function lstat(path, options) {
    return stat(path, options);
}

async function unlink(path) {
    return core.unlink(path);
}

async function mkdir(path, options) {
    const mode = typeof options === 'object' ? options.mode : options;
    return core.mkdir(path, mode ?? 0o777);
}

async function rmdir(path) {
    return core.rmdir(path);
}

async function rename(oldPath, newPath) {
    return core.rename(oldPath, newPath);
}

async function copyFile(src, dest, flags) {
    return core.copyFile(src, dest);
}

async function realpath(path) {
    return core.realPath(path);
}

async function readlink(path) {
    return core.readLink(path);
}

async function symlink(target, path, type) {
    let symlinkFlags = 0;
    if (type === 'dir' || type === 'directory') {
        symlinkFlags = core.FS_SYMLINK_DIR;
    } else if (type === 'junction') {
        symlinkFlags = core.FS_SYMLINK_JUNCTION;
    }
    return core.symlink(target, path, symlinkFlags);
}

async function link(existingPath, newPath) {
    return core.link(existingPath, newPath);
}

async function chmod(path, mode) {
    return core.chmod(path, mode);
}

async function access(path, mode) {
    try {
        await core.stat(path);
        return undefined;
    } catch (e) {
        throw new Error('access not implemented');
    }
}

async function open(path, flags, mode) {
    return core.open(path, flags, mode ?? 0o666);
}

export {
    readFile,
    writeFile,
    appendFile,
    readdir,
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
    access,
    open,
};

export default {
    readFile,
    writeFile,
    appendFile,
    readdir,
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
    access,
    open,
};
