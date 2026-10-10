function defaultEscape(str) {
    return encodeURIComponent(str);
}

function defaultUnescape(str) {
    try {
        return decodeURIComponent(str);
    } catch {
        return str;
    }
}

function stringifyPrimitive(value) {
    if (typeof value === 'string') {
        return value;
    }
    if (typeof value === 'number') {
        return Number.isFinite(value) ? String(value) : '';
    }
    if (typeof value === 'bigint') {
        return String(value);
    }
    if (typeof value === 'boolean') {
        return value ? 'true' : 'false';
    }
    return '';
}

function stringify(obj, sep = '&', eq = '=', options = undefined) {
    if (obj == null || typeof obj !== 'object') {
        return '';
    }

    const encode = typeof options?.encodeURIComponent === 'function'
        ? options.encodeURIComponent
        : defaultEscape;
    const pairs = [];

    for (const key of Object.keys(obj)) {
        const value = obj[key];
        const encodedKey = encode(stringifyPrimitive(key));

        if (Array.isArray(value)) {
            for (const item of value) {
                pairs.push(`${encodedKey}${eq}${encode(stringifyPrimitive(item))}`);
            }
            continue;
        }

        pairs.push(`${encodedKey}${eq}${encode(stringifyPrimitive(value))}`);
    }

    return pairs.join(sep);
}

function parse(qs, sep = '&', eq = '=', options = undefined) {
    const query = typeof qs === 'string' ? qs : String(qs ?? '');
    const decode = typeof options?.decodeURIComponent === 'function'
        ? options.decodeURIComponent
        : defaultUnescape;
    const maxKeys = options?.maxKeys === undefined ? 1000 : options.maxKeys;
    const out = Object.create(null);

    if (query.length === 0) {
        return out;
    }

    const parts = query.split(sep);
    const limit = maxKeys > 0 ? Math.min(parts.length, maxKeys) : parts.length;

    for (let i = 0; i < limit; i++) {
        const entry = parts[i];
        if (entry.length === 0) {
            continue;
        }

        const pos = entry.indexOf(eq);
        const rawKey = pos >= 0 ? entry.slice(0, pos) : entry;
        const rawValue = pos >= 0 ? entry.slice(pos + eq.length) : '';
        const key = decode(rawKey.replace(/\+/g, '%20'));
        const value = decode(rawValue.replace(/\+/g, '%20'));
        const existing = out[key];

        if (existing === undefined) {
            out[key] = value;
        } else if (Array.isArray(existing)) {
            existing.push(value);
        } else {
            out[key] = [existing, value];
        }
    }

    return out;
}

const querystring = {
    parse,
    stringify,
    encode: stringify,
    decode: parse,
    escape: defaultEscape,
    unescape: defaultUnescape,
};

export const encode = stringify;
export const decode = parse;
export const escape = defaultEscape;
export const unescape = defaultUnescape;
export { parse, stringify };
export default querystring;
