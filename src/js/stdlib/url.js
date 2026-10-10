const core = globalThis[Symbol.for('tjs.internal.core')];
const native = core.nodeUrl;

export const URL = globalThis.URL;
export const URLSearchParams = globalThis.URLSearchParams;
export const URLPattern = globalThis.URLPattern;
export const fileURLToPath = native.fileURLToPath;
export const pathToFileURL = native.pathToFileURL;
export const format = native.format;
export const domainToASCII = native.domainToASCII;
export const domainToUnicode = native.domainToUnicode;

function parse(urlStr, parseQueryString = false) {
    const u = new URL(urlStr, 'http://localhost');
    let auth = null;
    if (u.username) {
        if (u.password) {
            auth = `${u.username}:${u.password}`;
        } else {
            auth = u.username;
        }
    }

    const query = parseQueryString ? Object.fromEntries(u.searchParams) : u.search?.slice(1) || '';

    return {
        href: u.href,
        protocol: u.protocol,
        slashes: true,
        auth,
        host: u.host,
        port: u.port || null,
        hostname: u.hostname,
        hash: u.hash || null,
        search: u.search || null,
        query,
        pathname: u.pathname,
        path: u.pathname + (u.search || ''),
    };
}

const api = {
    URL,
    URLSearchParams,
    URLPattern,
    fileURLToPath,
    pathToFileURL,
    format,
    domainToASCII,
    domainToUnicode,
    parse,
};

export default api;
