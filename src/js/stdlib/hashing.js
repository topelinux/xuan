import md5 from 'js-md5';
import sha1 from 'js-sha1';
import { sha256, sha224 } from 'js-sha256';
import { sha3_512, sha3_384, sha3_256, sha3_224 } from 'js-sha3';
import { sha512, sha384, sha512_256, sha512_224 } from 'js-sha512';


const kHashObj = Symbol('kHashObj');
const supportedHashes = {
    md5,
    sha1,
    sha256,
    sha224,
    sha512,
    sha384,
    sha512_256,
    sha512_224,
    sha3_512,
    sha3_384,
    sha3_256,
    sha3_224
};

const hashAliases = {
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

function normalizeHashType(type) {
    const name = String(type).toLowerCase();
    return hashAliases[name] ?? name;
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

class Hash {
    constructor(obj) {
        this[kHashObj] = obj.create();
    }

    update(data) {
        this[kHashObj].update(data);

        return this;
    }

    digest(encoding) {
        if (encoding == null) {
            return this[kHashObj].hex();
        }

        return formatDigest(this.bytes(), encoding);
    }

    bytes() {
        return new Uint8Array(this[kHashObj].array());
    }
}

function createHash(type) {
    const obj = supportedHashes[normalizeHashType(type)];

    if (!obj) {
        throw new TypeError('Invalid hash type: ' + type);
    }

    return new Hash(obj);
}

const SUPPORTED_TYPES = Object.keys(supportedHashes).concat(Object.keys(hashAliases));

export { createHash, SUPPORTED_TYPES };
