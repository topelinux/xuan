import { Buffer } from 'tjs:buffer';

function normalizeEncoding(encoding) {
    const enc = String(encoding || 'utf8').toLowerCase().replace(/[-_]/g, '');
    if (enc === 'utf8') return 'utf8';
    if (enc === 'utf16le' || enc === 'ucs2' || enc === 'ucs') return 'utf16le';
    if (enc === 'latin1' || enc === 'binary') return 'latin1';
    if (enc === 'ascii') return 'ascii';
    if (enc === 'base64') return 'base64';
    if (enc === 'hex') return 'hex';
    throw new TypeError(`Unknown encoding: ${encoding}`);
}

function concatBuffer(a, b) {
    if (!a || a.length === 0) return b;
    if (!b || b.length === 0) return a;
    return Buffer.concat([ a, b ], a.length + b.length);
}

function splitRemainder(buf, unitSize) {
    const remaining = buf.length % unitSize;
    if (remaining === 0) {
        return { body: buf, pending: Buffer.alloc(0) };
    }
    return {
        body: buf.subarray(0, buf.length - remaining),
        pending: buf.subarray(buf.length - remaining),
    };
}

function StringDecoder(encoding = 'utf8') {
    if (this == null || this === globalThis) {
        return new StringDecoder(encoding);
    }
    this.encoding = normalizeEncoding(encoding);
    this.lastNeed = 0;
    this.lastTotal = 0;
    this.lastChar = Buffer.alloc(4);
    this._pending = Buffer.alloc(0);
    this._decoder = this.encoding === 'utf8' ? new TextDecoder('utf-8') : null;
}

StringDecoder.prototype.write = function write(buf) {
    if (buf == null || buf.length === 0) {
        return '';
    }

    const input = Buffer.isBuffer(buf) ? buf : Buffer.from(buf);
    if (this.encoding === 'utf8') {
        return this._decoder.decode(input, { stream: true });
    }

    const data = concatBuffer(this._pending, input);
    this._pending = Buffer.alloc(0);

    if (this.encoding === 'utf16le') {
        const split = splitRemainder(data, 2);
        this._pending = split.pending;
        return split.body.toString('utf16le');
    }

    if (this.encoding === 'base64') {
        const split = splitRemainder(data, 3);
        this._pending = split.pending;
        return split.body.toString('base64');
    }

    if (this.encoding === 'hex') {
        return data.toString('hex');
    }

    return data.toString(this.encoding);
};

StringDecoder.prototype.end = function end(buf) {
    let result = '';
    if (buf != null && buf.length !== 0) {
        result = this.write(buf);
    }

    if (this.encoding === 'utf8') {
        return result + this._decoder.decode();
    }

    if (this._pending.length !== 0) {
        result += this._pending.toString(this.encoding);
        this._pending = Buffer.alloc(0);
    }
    return result;
};

const defaultExport = { StringDecoder };

export { StringDecoder };
export default defaultExport;
