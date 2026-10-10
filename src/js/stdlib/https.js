import http from 'tjs:http';

export const STATUS_CODES = http.STATUS_CODES;

function normalizeArgs(args) {
    const list = [ ...args ];
    const callback = typeof list[list.length - 1] === 'function' ? list.pop() : undefined;

    let inputURL = undefined;
    let options = undefined;

    if (typeof list[0] === 'string' || (typeof URL !== 'undefined' && list[0] instanceof URL)) {
        inputURL = list.shift();
    }
    if (list.length > 0) {
        options = list.shift();
    }
    if (list.length > 0) {
        throw new TypeError('Too many arguments');
    }

    const normalizedOptions = { ...(options ?? {}) };
    if (normalizedOptions.protocol == null) {
        normalizedOptions.protocol = 'https:';
    }

    const out = [];
    if (inputURL != null) out.push(inputURL);
    out.push(normalizedOptions);
    if (callback) out.push(callback);
    return out;
}

export function request(...args) {
    return http.request(...normalizeArgs(args));
}

export function get(...args) {
    const req = request(...args);
    req.end();
    return req;
}

export function createServer() {
    throw new Error('https is not supported in this build');
}

export default {
    STATUS_CODES,
    request,
    get,
    createServer,
};
