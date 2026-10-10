import assert from 'tjs:assert';
import { ClientHttp2Session, connect, ClientHttp2Stream } from 'tjs:http2';

// R5: Verify TLS / ALPN / certificate observability fields on session.socket.
//
// grpc-js reads these fields from session.socket for channelz/logging:
//   - alpnProtocol: 'h2' for TLS h2, null for h2c
//   - authorized: boolean
//   - authorizationError: string or null
//   - encrypted: boolean
//   - servername: string or null
//   - getCipher(): { name, standardName, version }
//   - getPeerCertificate(): object
//   - getCertificate(): object
//   - getProtocol(): string or null
//
// These must be stable (not crash) even when values are stubbed.

// Helper: verify socket shape matches what grpc-js expects
function verifySocketShape(socket, label) {
    assert.ok(socket !== null && socket !== undefined,
        `${label}: socket is not null/undefined`);
    assert.ok(typeof socket === 'object',
        `${label}: socket is an object`);

    // alpnProtocol: 'h2' for TLS, null for h2c
    assert.ok(socket.alpnProtocol === null || socket.alpnProtocol === 'h2' ||
              socket.alpnProtocol === undefined,
        `${label}: alpnProtocol is h2, null, or undefined (got: ${socket.alpnProtocol})`);

    // authorized: boolean
    assert.ok(socket.authorized === undefined || typeof socket.authorized === 'boolean',
        `${label}: authorized is boolean (got: ${typeof socket.authorized})`);

    // authorizationError: string or null
    assert.ok(socket.authorizationError === null ||
              socket.authorizationError === undefined ||
              typeof socket.authorizationError === 'string',
        `${label}: authorizationError is string or null`);

    // encrypted: boolean
    assert.ok(socket.encrypted === undefined || typeof socket.encrypted === 'boolean',
        `${label}: encrypted is boolean (got: ${typeof socket.encrypted})`);

    // servername: string or null
    assert.ok(socket.servername === null || socket.servername === undefined ||
              typeof socket.servername === 'string',
        `${label}: servername is string or null`);

    // getCipher() must not crash
    const cipher = socket.getCipher ? socket.getCipher() : null;
    if (cipher !== null && cipher !== undefined) {
        assert.ok(typeof cipher === 'object',
            `${label}: getCipher returns object`);
    }

    // getPeerCertificate() must not crash
    const peerCert = socket.getPeerCertificate ? socket.getPeerCertificate() : null;
    if (peerCert !== null && peerCert !== undefined) {
        assert.ok(typeof peerCert === 'object',
            `${label}: getPeerCertificate returns object`);
    }

    // getCertificate() must not crash
    const cert = socket.getCertificate ? socket.getCertificate() : null;
    if (cert !== null && cert !== undefined) {
        assert.ok(typeof cert === 'object',
            `${label}: getCertificate returns object`);
    }

    // getProtocol() must not crash
    const proto = socket.getProtocol ? socket.getProtocol() : null;
    if (proto !== null && proto !== undefined) {
        assert.ok(typeof proto === 'string',
            `${label}: getProtocol returns string (got: ${typeof proto})`);
    }
}

// === Test 1: ClientHttp2Session prototype has socket getter ===
{
    const proto = ClientHttp2Session.prototype;
    assert.ok('socket' in proto, 'socket getter exists on prototype');
    assert.ok('encrypted' in proto, 'encrypted getter exists on prototype');
}

// === Test 2: session.socket methods don't crash (bare socket) ===
{
    const core = globalThis[Symbol.for('tjs.internal.core')];
    const NativeH2Session = core.H2Session;
    if (typeof NativeH2Session !== 'function') {
        console.log('SKIP: native H2Session not available');
    } else {
        // Create a bare session (no real connection, no socket_obj yet).
        // This tests that grpc-js accessing session.socket doesn't crash
        // even when socket is null/undefined.
        const native = new NativeH2Session();
        const session = new ClientHttp2Session(native);

        // session.socket may be null/undefined before connect — shouldn't crash
        const sock = session.socket;
        assert.ok(sock === null || sock === undefined || typeof sock === 'object',
            'session.socket before connect is null/undefined/object (no crash)');

        session.destroy();
    }
}

// === Test 3: session.socket getter should reflect late native socket assignment ===
{
    const core = globalThis[Symbol.for('tjs.internal.core')];
    const NativeH2Session = core.H2Session;
    if (typeof NativeH2Session !== 'function') {
        console.log('SKIP: native H2Session not available');
    } else {
        const native = new NativeH2Session();
        const session = new ClientHttp2Session(native);
        assert.ok(session.socket === null || session.socket === undefined,
            'session.socket is null/undefined before native socket appears');

        // Simulate native connect() creating socket_obj later than JS wrapper construction.
        const lateSock = { encrypted: true, alpnProtocol: 'h2' };
        native.socket = lateSock;
        assert.eq(session.socket, lateSock,
            'session.socket reflects late native.socket assignment');

        session.destroy();
    }
}

// === Test 4: createConnection override should win over native socket ===
{
    const core = globalThis[Symbol.for('tjs.internal.core')];
    const NativeH2Session = core.H2Session;
    if (typeof NativeH2Session !== 'function') {
        console.log('SKIP: native H2Session not available');
    } else {
        const native = new NativeH2Session();
        const overrideSock = { encrypted: true, alpnProtocol: 'h2' };
        const session = new ClientHttp2Session(native, overrideSock);
        native.socket = { encrypted: false, alpnProtocol: null };
        assert.eq(session.socket, overrideSock,
            'socket override has higher priority than native socket');
        verifySocketShape(session.socket, 'override session');
        session.destroy();
    }
}
