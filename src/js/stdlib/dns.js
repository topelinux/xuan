/**
 * Minimal dns module for @grpc/grpc-js compatibility.
 *
 * Provides only what grpc-js actually uses:
 *   - dns.promises.Resolver (class, mostly stub)
 *   - dns.promises.lookup(hostname, { all })
 *   - dns.promises.resolveTxt(hostname) (stub, returns [])
 *
 * Uses core.getaddrinfo() under the hood.
 */

const core = globalThis[Symbol.for('tjs.internal.core')];

// Address families for getaddrinfo() input
const AF_INET = core.AF_INET;
const AF_INET6 = core.AF_INET6;
const AF_UNSPEC = core.AF_UNSPEC;

function familyToNumber(family) {
    if (family === 'IPv4' || family === 4) return AF_INET;
    if (family === 'IPv6' || family === 6) return AF_INET6;
    return AF_UNSPEC;
}

function familyToNodeValue(family) {
    if (family === AF_INET || family === 4) return 4;
    if (family === AF_INET6 || family === 6) return 6;
    return 0;
}

/**
 * dns.promises.lookup(hostname, options) -> { address, family } | [{ address, family }, ...]
 */
async function lookup(hostname, options = {}) {
    if (typeof hostname !== 'string') {
        throw new TypeError('hostname must be a string');
    }

    const lookupOptions = typeof options === 'number' ? { family: options } : (options || {});
    const family = familyToNumber(lookupOptions.family || AF_UNSPEC);
    const res = await core.getaddrinfo(hostname, { family });

    if (!res || res.length === 0) {
        const err = new Error(`getaddrinfo ENOTFOUND ${hostname}`);
        err.code = 'ENOTFOUND';
        err.errno = -3008; // UV_ENOTFOUND
        err.hostname = hostname;
        throw err;
    }

    const results = res.map(item => ({
        address: item.ip,
        family: familyToNodeValue(item.family)
    }));

    if (lookupOptions.all) {
        return results;
    }
    return results[0];
}

/**
 * dns.promises.resolveTxt(hostname) -> string[][]
 * Stub: returns empty array. grpc-js uses this for service discovery.
 */
async function resolveTxt(_hostname) {
    return [];
}

/**
 * dns.promises.Resolver - minimal stub class.
 * grpc-js instantiates this for alternative resolution.
 */
class Resolver {
    constructor(_options) {
        // Stub: no options needed for basic resolution
    }

    setServers(_servers) {
        // Stub: ignore server list. We use system resolver.
    }

    async resolve4(hostname) {
        const res = await core.getaddrinfo(hostname, { family: AF_INET });
        return (res || []).map(item => item.ip);
    }

    async resolve6(hostname) {
        const res = await core.getaddrinfo(hostname, { family: AF_INET6 });
        return (res || []).map(item => item.ip);
    }

    async resolveTxt(_hostname) {
        return [];
    }

    cancel() {
        // Stub: no-op
    }
}

// Export in Node.js dns module shape
const promises = {
    lookup,
    resolveTxt,
    Resolver
};

export {
    promises,
    lookup,
    resolveTxt,
    Resolver
};

export default {
    promises,
    lookup,
    resolveTxt,
    Resolver
};
