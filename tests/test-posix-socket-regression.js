import assert from 'tjs:assert';
import FFI from 'tjs:ffi';
import { spawnSync } from 'node:child_process';
import { PosixSocket } from 'tjs:posix-socket';

function createPtyPair() {
    const libc = new FFI.Lib(FFI.Lib.LIBC_NAME);
    const posixOpenPt = new FFI.CFunction(libc.symbol('posix_openpt'), FFI.types.sint, [FFI.types.sint]);
    const grantPt = new FFI.CFunction(libc.symbol('grantpt'), FFI.types.sint, [FFI.types.sint]);
    const unlockPt = new FFI.CFunction(libc.symbol('unlockpt'), FFI.types.sint, [FFI.types.sint]);
    const ptsName = new FFI.CFunction(libc.symbol('ptsname'), FFI.types.string, [FFI.types.sint]);
    const openFn = new FFI.CFunction(libc.symbol('open'), FFI.types.sint, [FFI.types.string, FFI.types.sint]);

    const O_RDWR = 2;
    const masterFd = posixOpenPt.call(O_RDWR);
    assert.ok(masterFd >= 0);
    assert.eq(grantPt.call(masterFd), 0);
    assert.eq(unlockPt.call(masterFd), 0);

    const slavePath = ptsName.call(masterFd);
    assert.ok(typeof slavePath === 'string' && slavePath.length > 0);
    const slaveFd = openFn.call(slavePath, O_RDWR);
    assert.ok(slaveFd >= 0);

    return { masterFd, slaveFd };
}

function run() {
    if (navigator.userAgentData.platform === 'Windows') {
        return;
    }

    const { masterFd, slaveFd } = createPtyPair();
    const master = PosixSocket.createFromFD(masterFd);
    const slave = PosixSocket.createFromFD(slaveFd);
    const encoder = new TextEncoder();
    const decoder = new TextDecoder();

    try {
        // Regression: polling getter must not throw before poll() is initialized.
        assert.eq(master.polling, false);

        // Regression: createFromFD read() should use exact JS size value on 64-bit.
        assert.eq(slave.write(encoder.encode('A')), 1);
        assert.eq(decoder.decode(master.read(1)), 'A');

        assert.eq(master.polling, false);
    } finally {
        slave.close();
        master.close();
    }
}

async function runPollCloseGcRegression() {
    if (navigator.userAgentData.platform === 'Windows') {
        return;
    }

    let sock = null;
    try {
        const { masterFd } = createPtyPair();
        sock = PosixSocket.createFromFD(masterFd);
        sock.poll({ read: () => {} });
        assert.eq(sock.polling, true);
        sock.close();
        sock = null;

        tjs.engine.gc.run();
        await new Promise(resolve => setTimeout(resolve, 0));
    } finally {
        if (sock) {
            sock.close();
        }
    }
}

async function runPollCallbackExceptionRegression() {
    if (navigator.userAgentData.platform === 'Windows') {
        return;
    }

    const script = `
        const FFI = require('tjs:ffi');
        const { PosixSocket } = require('tjs:posix-socket');
        function createPtyPair() {
            const libc = new FFI.Lib(FFI.Lib.LIBC_NAME);
            const posixOpenPt = new FFI.CFunction(libc.symbol('posix_openpt'), FFI.types.sint, [FFI.types.sint]);
            const grantPt = new FFI.CFunction(libc.symbol('grantpt'), FFI.types.sint, [FFI.types.sint]);
            const unlockPt = new FFI.CFunction(libc.symbol('unlockpt'), FFI.types.sint, [FFI.types.sint]);
            const ptsName = new FFI.CFunction(libc.symbol('ptsname'), FFI.types.string, [FFI.types.sint]);
            const openFn = new FFI.CFunction(libc.symbol('open'), FFI.types.sint, [FFI.types.string, FFI.types.sint]);
            const O_RDWR = 2;
            const masterFd = posixOpenPt.call(O_RDWR);
            grantPt.call(masterFd);
            unlockPt.call(masterFd);
            const slaveFd = openFn.call(ptsName.call(masterFd), O_RDWR);
            return { masterFd, slaveFd };
        }
        const { masterFd, slaveFd } = createPtyPair();
        const master = PosixSocket.createFromFD(masterFd);
        const slave = PosixSocket.createFromFD(slaveFd);
        master.poll({ read() { throw new TypeError('poll boom'); } });
        slave.write(new TextEncoder().encode('B'));
        setTimeout(() => {}, 50);
    `;
    const ret = spawnSync(process.execPath, [ 'eval', script ], { encoding: 'utf8' });
    const exitCode = ret.status ?? ret.exit_status;
    assert.ok(exitCode === 1 || exitCode === -1);
    assert.ok(ret.stderr.includes('poll boom'));
}

run();
await runPollCloseGcRegression();
await runPollCallbackExceptionRegression();
