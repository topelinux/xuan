const core = globalThis[Symbol.for('tjs.internal.core')];
const HttpServer = core.HttpServer;

// Hop-by-hop headers managed by lws; must not be passed through for streaming responses.
const kHopByHopHeaders = new Set([ 'transfer-encoding', 'connection', 'content-length', 'keep-alive' ]);

const kWsUpgrade = Symbol('kWsUpgrade');

function getRequestAuthority(headers) {
    const rawHost = headers.get('host');

    if (!rawHost) {
        return 'localhost';
    }

    const parts = String(rawHost).split(',');

    for (let i = parts.length - 1; i >= 0; i--) {
        const value = parts[i].trim();

        if (value) {
            return value;
        }
    }

    return 'localhost';
}

function normalizeRequestTarget(url) {
    const value = String(url || '/');

    if (value.startsWith('/') || value.startsWith('?')) {
        return value;
    }

    return `/${value}`;
}

function buildRequestURL(headers, url) {
    const value = String(url || '/');

    if (/^[A-Za-z][A-Za-z0-9+.-]*:\/\//.test(value)) {
        try {
            new URL(value);
            return value;
        } catch {
            // Fall through and salvage a path-only request target.
        }
    }

    const target = normalizeRequestTarget(value);
    const fullUrl = `http://${getRequestAuthority(headers)}${target}`;

    try {
        new URL(fullUrl);
        return fullUrl;
    } catch {
        return `http://localhost${target}`;
    }
}

class Server {
    #handle;
    #handler;

    constructor(options) {
        if (typeof options === 'function') {
            options = { fetch: options };
        }

        const { fetch: handler, port = 0, listenIp = '0.0.0.0', websocket } = options;

        if (typeof handler !== 'function') {
            throw new TypeError('fetch handler must be a function');
        }

        this.#handler = handler;

        const wsOpen = websocket?.open ?? null;
        const wsMessage = websocket?.message ?? null;
        const wsClose = websocket?.close ?? null;
        const wsError = websocket?.error ?? null;
        const wsPing = websocket?.ping ?? null;
        const wsPong = websocket?.pong ?? null;

        const onRequest = (requestId, method, url, headersArr, bodyBuffer, remoteAddr, isWsUpgrade) => {
            if (isWsUpgrade) {
                this.#handleWsUpgrade(requestId, method, url, headersArr, remoteAddr);
            } else {
                this.#handleRequest(requestId, method, url, headersArr, bodyBuffer, remoteAddr);
            }
        };

        this.#handle = new HttpServer(port, listenIp, onRequest, wsOpen, wsMessage, wsClose, wsError, wsPing, wsPong);
    }

    get port() {
        return this.#handle.port;
    }

    close(callback) {
        const onClosed = typeof callback === 'function'
            ? () => setTimeout(callback, 0)
            : undefined;
        this.#handle.close(onClosed);
    }

    upgrade(request, options) {
        const upgradeId = request[kWsUpgrade];

        // eslint-disable-next-line eqeqeq
        if (upgradeId == null) {
            return false;
        }

        const data = options?.data ?? null;

        return this.#handle.acceptUpgrade(upgradeId, data);
    }

    #handleWsUpgrade(upgradeId, method, url, headersArr, remoteAddr) {
        const headers = Server.#parseHeaders(headersArr);
        const fullUrl = buildRequestURL(headers, url);
        const request = new Request(fullUrl, { method, headers });

        request[kWsUpgrade] = upgradeId;
        this.#handler(request, { server: this, remoteAddress: remoteAddr });
        // server.upgrade(req) must have been called synchronously
    }

    async #handleRequest(requestId, method, url, headersArr, bodyBuffer, remoteAddr) {
        let headersSent = false;

        try {
            const headers = Server.#parseHeaders(headersArr);

            // Build Request.
            const fullUrl = buildRequestURL(headers, url);
            const requestInit = { method, headers };

            if (bodyBuffer && bodyBuffer.byteLength > 0 && method !== 'GET' && method !== 'HEAD') {
                requestInit.body = bodyBuffer;
            }

            const request = new Request(fullUrl, requestInit);

            // Call user handler.
            let response = this.#handler(request, { server: this, remoteAddress: remoteAddr });

            if (response instanceof Promise) {
                response = await response;
            }

            if (!(response instanceof Response)) {
                response = new Response('Internal Server Error', { status: 500 });
            }

            // Extract response data.
            const status = response.status;
            const responseHeaders = [];

            response.headers.forEach((value, name) => {
                responseHeaders.push([ name, value ]);
            });

            if (response.body && response._bodySize < 0) {
                // Streaming: send headers first, then stream body chunks.
                // Filter hop-by-hop headers that lws manages itself.
                const streamHeaders = responseHeaders.filter(
                    ([ name ]) => !kHopByHopHeaders.has(name),
                );

                this.#handle.sendHeaders(requestId, status, streamHeaders);
                headersSent = true;

                const reader = response.body.getReader();


                while (true) {
                    const { done, value } = await reader.read();

                    if (done) {
                        this.#handle.sendBody(requestId, null, true);
                        break;
                    }

                    this.#handle.sendBody(requestId, value, false);
                }
            } else {
                // Buffered: read entire body and send at once.
                let body = null;

                if (response.body) {
                    const buf = await response.arrayBuffer();

                    body = new Uint8Array(buf);
                }

                this.#handle.sendResponse(requestId, status, responseHeaders, body);
            }
        } catch (_error) {
            if (headersSent) {
                // Headers already sent, close the stream.
                try {
                    this.#handle.sendBody(requestId, null, true);
                } catch {
                    // Nothing we can do.
                }
            } else {
                try {
                    const errorBody = new TextEncoder().encode('Internal Server Error');

                    this.#handle.sendResponse(
                        requestId, 500, [ [ 'content-type', 'text/plain' ] ], errorBody,
                    );
                } catch {
                    // Nothing we can do.
                }
            }
        }
    }

    static #parseHeaders(headersArr) {
        const headers = new Headers();

        for (let i = 0; i < headersArr.length; i += 2) {
            headers.append(headersArr[i], headersArr[i + 1]);
        }

        return headers;
    }
}

export function serve(options) {
    return new Server(options);
}
