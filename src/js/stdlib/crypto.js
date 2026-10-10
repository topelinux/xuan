import { createHash, SUPPORTED_TYPES } from 'tjs:hashing';

const core = globalThis[Symbol.for('tjs.internal.core')];
const textEncoder = new TextEncoder();
const NativeHmac = core.crypto?.Hmac;

const HMAC_BLOCK_SIZES = {
    md5: 64,
    sha1: 64,
    sha224: 64,
    sha256: 64,
    sha384: 128,
    sha512: 128,
    sha512_224: 128,
    sha512_256: 128,
    sha3_224: 144,
    sha3_256: 136,
    sha3_384: 104,
    sha3_512: 72
};

const HASH_ALIASES = {
    'sha-1': 'sha1',
    'sha-224': 'sha224',
    'sha-256': 'sha256',
    'sha-384': 'sha384',
    'sha-512': 'sha512',
    'sha512-224': 'sha512_224',
    'sha512-256': 'sha512_256',
    'sha3-224': 'sha3_224',
    'sha3-256': 'sha3_256',
    'sha3-384': 'sha3_384',
    'sha3-512': 'sha3_512'
};

const NATIVE_HMAC_TYPES = new Set([
    'md5',
    'sha1',
    'sha224',
    'sha256',
    'sha384',
    'sha512',
    'sha3_224',
    'sha3_256',
    'sha3_384',
    'sha3_512'
]);

function normalizeHashType(type) {
    const name = String(type).toLowerCase();
    return HASH_ALIASES[name] ?? name;
}

function toBytes(data, encoding = 'utf8') {
    if (typeof data === 'string') {
        if (globalThis.Buffer) {
            return globalThis.Buffer.from(data, encoding);
        }

        if (encoding !== 'utf8' && encoding !== 'utf-8') {
            throw new TypeError('Unsupported encoding without Buffer: ' + encoding);
        }

        return textEncoder.encode(data);
    }

    if (data instanceof ArrayBuffer) {
        return new Uint8Array(data);
    }

    if (ArrayBuffer.isView(data)) {
        return new Uint8Array(data.buffer, data.byteOffset, data.byteLength);
    }

    throw new TypeError('data must be a string, Buffer, TypedArray, or ArrayBuffer');
}

function formatDigest(bytes, encoding) {
    if (encoding == null || encoding === 'hex') {
        let output = '';
        for (let i = 0; i < bytes.length; i++) {
            output += bytes[i].toString(16).padStart(2, '0');
        }

        return output;
    }

    if (encoding === 'buffer') {
        return globalThis.Buffer ? globalThis.Buffer.from(bytes) : bytes;
    }

    if (!globalThis.Buffer) {
        throw new TypeError('Digest encoding requires Buffer support: ' + encoding);
    }

    return globalThis.Buffer.from(bytes).toString(encoding);
}

function getHashes() {
    return [ ...SUPPORTED_TYPES ];
}

class Hmac {
    constructor(algorithm, key, options = undefined) {
        this.algorithm = normalizeHashType(algorithm);

        const blockSize = HMAC_BLOCK_SIZES[this.algorithm];
        if (!blockSize) {
            throw new TypeError('Invalid hash type: ' + algorithm);
        }

        const keyEncoding = typeof options === 'object' && options ? options.encoding : undefined;
        let keyBytes = toBytes(key, keyEncoding);
        if (NativeHmac && NATIVE_HMAC_TYPES.has(this.algorithm)) {
            this.native = new NativeHmac(this.algorithm, keyBytes);
            this.finalized = false;
            return;
        }

        if (keyBytes.length > blockSize) {
            keyBytes = createHash(this.algorithm).update(keyBytes).bytes();
        }

        const innerPad = new Uint8Array(blockSize);
        const outerPad = new Uint8Array(blockSize);
        for (let i = 0; i < blockSize; i++) {
            const value = i < keyBytes.length ? keyBytes[i] : 0;
            innerPad[i] = value ^ 0x36;
            outerPad[i] = value ^ 0x5c;
        }

        this.outerPad = outerPad;
        this.innerHash = createHash(this.algorithm).update(innerPad);
        this.finalized = false;
    }

    update(data, inputEncoding = 'utf8') {
        if (this.finalized) {
            throw new Error('Digest already called');
        }

        if (this.native) {
            this.native.update(toBytes(data, inputEncoding));
            return this;
        }

        this.innerHash.update(toBytes(data, inputEncoding));
        return this;
    }

    digest(encoding) {
        if (this.finalized) {
            throw new Error('Digest already called');
        }

        this.finalized = true;
        if (this.native) {
            return formatDigest(new Uint8Array(this.native.digest()), encoding);
        }

        const innerDigest = this.innerHash.bytes();
        const bytes = createHash(this.algorithm)
            .update(this.outerPad)
            .update(innerDigest)
            .bytes();

        return formatDigest(bytes, encoding);
    }
}

function createHmac(algorithm, key, options) {
    return new Hmac(algorithm, key, options);
}

function timingSafeEqual(a, b) {
    const left = toBytes(a);
    const right = toBytes(b);
    if (left.byteLength !== right.byteLength) {
        throw new RangeError('Input buffers must have the same byte length');
    }

    let out = 0;
    for (let i = 0; i < left.byteLength; i++) {
        out |= left[i] ^ right[i];
    }
    return out === 0;
}

function randomBytes(size, callback) {
    if (typeof size !== 'number' || size < 0) {
        throw new TypeError('size must be a non-negative number');
    }
    
    const buffer = new Uint8Array(size);
    if (size > 0) {
        core.random(buffer.buffer, 0, size);
    }
    
    // Convert to Buffer if available
    const result = globalThis.Buffer ? globalThis.Buffer.from(buffer) : buffer;
    
    if (typeof callback === 'function') {
        queueMicrotask(() => callback(null, result));
        return undefined;
    }
    
    return result;
}

function randomFillSync(buffer, offset = 0, size = buffer.byteLength - offset) {
    if (!(buffer instanceof ArrayBuffer) && !ArrayBuffer.isView(buffer)) {
        throw new TypeError('buffer must be an ArrayBuffer or TypedArray');
    }
    
    if (buffer instanceof ArrayBuffer) {
        core.random(buffer, offset, size);
        return buffer;
    }
    
    core.random(buffer.buffer, buffer.byteOffset + offset, size);
    return buffer;
}

function randomFill(buffer, offset, size, callback) {
    if (typeof offset === 'function') {
        callback = offset;
        offset = 0;
        size = buffer.byteLength;
    } else if (typeof size === 'function') {
        callback = size;
        size = buffer.byteLength - offset;
    }
    
    if (typeof callback !== 'function') {
        throw new TypeError('callback must be a function');
    }
    
    try {
        const result = randomFillSync(buffer, offset, size);
        queueMicrotask(() => callback(null, result));
    } catch (err) {
        queueMicrotask(() => callback(err));
    }
}

export { createHash, createHmac, getHashes, randomBytes, randomFillSync, randomFill, timingSafeEqual };
export default { createHash, createHmac, getHashes, randomBytes, randomFillSync, randomFill, timingSafeEqual };
