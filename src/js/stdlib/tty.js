const core = globalThis[Symbol.for('tjs.internal.core')];

function isatty(fd) {
    if (typeof fd !== 'number') return false;
    return core.guessHandle(fd) === 'tty';
}

class ReadStream {
    constructor(fd) {
        this.fd = fd;
        this.isTTY = isatty(fd);
    }
}

class WriteStream {
    constructor(fd) {
        this.fd = fd;
        this.isTTY = isatty(fd);
    }
}

export {
    isatty,
    ReadStream,
    WriteStream
};

export default {
    isatty,
    ReadStream,
    WriteStream
};
