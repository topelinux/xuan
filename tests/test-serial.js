import assert from 'tjs:assert';
import FFI from 'tjs:ffi';
import { PosixSocket } from 'tjs:posix-socket';
import SerialPort from 'tjs:serial';

const encoder = new TextEncoder();
const decoder = new TextDecoder();

function onceEvent(emitter, eventName, timeoutMs = 2000) {
    return new Promise((resolve, reject) => {
        const timer = setTimeout(() => {
            emitter.off(eventName, onEvent);
            reject(new Error(`timed out waiting for '${eventName}'`));
        }, timeoutMs);

        function onEvent(...args) {
            clearTimeout(timer);
            emitter.off(eventName, onEvent);
            resolve(args);
        }

        emitter.on(eventName, onEvent);
    });
}

function waitReadable(sock, size = 1024, timeoutMs = 2000) {
    return new Promise((resolve, reject) => {
        let settled = false;

        function finish(err, value) {
            if (settled) {
                return;
            }
            settled = true;
            clearTimeout(timer);
            try {
                if (sock.polling) {
                    sock.stopPoll();
                }
            } catch {
            }
            if (err) {
                reject(err);
            } else {
                resolve(value);
            }
        }

        const timer = setTimeout(() => {
            finish(new Error('timed out waiting for readable data'));
        }, timeoutMs);

        sock.poll({
            read: () => {
                try {
                    const buf = sock.read(size);
                    setTimeout(() => finish(null, buf), 0);
                } catch (err) {
                    setTimeout(() => finish(err), 0);
                }
            },
            error: status => {
                setTimeout(() => finish(new Error(`poll error: ${status}`)), 0);
            }
        });
    });
}

function createPtyPair() {
    const libc = new FFI.Lib(FFI.Lib.LIBC_NAME);
    const posixOpenPt = new FFI.CFunction(libc.symbol('posix_openpt'), FFI.types.sint, [FFI.types.sint]);
    const grantPt = new FFI.CFunction(libc.symbol('grantpt'), FFI.types.sint, [FFI.types.sint]);
    const unlockPt = new FFI.CFunction(libc.symbol('unlockpt'), FFI.types.sint, [FFI.types.sint]);
    const ptsName = new FFI.CFunction(libc.symbol('ptsname'), FFI.types.string, [FFI.types.sint]);

    const O_RDWR = 2;
    const masterFd = posixOpenPt.call(O_RDWR);
    assert.ok(masterFd >= 0);
    assert.eq(grantPt.call(masterFd), 0);
    assert.eq(unlockPt.call(masterFd), 0);

    const slavePath = ptsName.call(masterFd);
    assert.ok(typeof slavePath === 'string' && slavePath.length > 0);

    return { masterFd, slavePath };
}

async function run() {
    if (navigator.userAgentData.platform === 'Windows') {
        return;
    }

    const serialPath = tjs.env.SERIAL_PORT_PATH;
    let masterSock = null;
    let slavePath;
    if (serialPath) {
        slavePath = serialPath;
    } else {
        const ptyPair = createPtyPair();
        slavePath = ptyPair.slavePath;
        masterSock = PosixSocket.createFromFD(ptyPair.masterFd);
    }

    console.log('in run', serialPath);
    const serial = new SerialPort({
        path: slavePath,
        baudRate: 115200,
        autoOpen: false
    });

    try {
        const openEvent = onceEvent(serial, 'open');
        await Promise.all([ serial.open(), openEvent ]);
        assert.ok(serial.isOpen);
        if (masterSock) {
            // Serial -> PTY master
            await serial.write('ping');
            const fromSerial = await waitReadable(masterSock, 64);
            assert.ok(fromSerial instanceof Uint8Array);
            console.log('recv ->', decoder.decode(fromSerial));
            assert.eq(decoder.decode(fromSerial), 'ping');

            // PTY master -> Serial
            const dataPromise = onceEvent(serial, 'data');
            assert.eq(masterSock.write(encoder.encode('pong')), 4);
            const [fromMaster] = await dataPromise;
            assert.ok(fromMaster instanceof Uint8Array);
            console.log('recv ->', decoder.decode(fromMaster));
            assert.eq(decoder.decode(fromMaster), 'pong');
        } else {
            // Physical loopback mode: TX/RX shorted, expect self-echo.
            console.log('in serial loopback demo');
            const dataPromise = onceEvent(serial, 'data', 5000);
            await serial.write('ping');
            await serial.drain();
            console.log('after write');
            const [echo] = await dataPromise;
            console.log('recv ->', decoder.decode(echo));
            assert.ok(decoder.decode(echo).includes('ping'));
        }
    } finally {
        await serial.close();
        if (masterSock) {
            masterSock.close();
        }
    }
}

run();
