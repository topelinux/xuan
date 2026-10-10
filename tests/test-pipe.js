const encoder = new TextEncoder();
const decoder = new TextDecoder();

function eq(actual, expected, message = 'values should be equal') {
    if (!Object.is(actual, expected)) {
        throw new Error(`${message}: expected ${expected}, got ${actual}`);
    }
}

function isNot(actual, unexpected, message = 'value should differ') {
    if (Object.is(actual, unexpected)) {
        throw new Error(`${message}: value ${actual}`);
    }
}

async function exists(path) {
    try {
        await tjs.stat(path);
        return true;
    } catch (error) {
        if (error?.code === 'ENOENT') {
            return false;
        }

        throw error;
    }
}

let pipeName;
if (navigator.userAgentData.platform === 'Windows') {
    pipeName = '\\\\?\\pipe\\testPipe';
} else {
    const pipeDir = `test-tmp/${Date.now()}-${tjs.pid}`;
    await tjs.makeDir(pipeDir, { recursive: true });
    pipeName = `${pipeDir}/test-pipe.sock`;
}

async function doEchoServer(serverReadable) {
    const reader = serverReadable.getReader();
    const { value: conn } = await reader.read();

    if (!conn) {
        return;
    }

    const { readable, writable } = await conn.opened;

    await readable.pipeTo(writable);
}

const server = new PipeServerSocket(pipeName);
const { readable: serverReadable, localAddress } = await server.opened;

const serverTask = doEchoServer(serverReadable);

const client = new PipeSocket(localAddress);
const { readable, writable } = await client.opened;

const writer = writable.getWriter();
const reader = readable.getReader();
await writer.write(encoder.encode('PING'));
let { value } = await reader.read();
let dataStr = decoder.decode(value);
eq(dataStr, 'PING', 'sending works');

await writer.close();
const eof = await reader.read();
eq(eof.done, true, 'client should observe EOF');

client.close();
server.close();
await serverTask;
await server.closed;

if (navigator.userAgentData.platform !== 'Windows') {
    eq(await exists(pipeName), false, 'server close should unlink unix socket path');
}

let error;
try {
    new PipeServerSocket();
} catch (e) {
    error = e;
}
isNot(error, undefined, 'PipeServerSocket() should throw');
eq(error.name, 'TypeError', 'PipeServerSocket() should throw TypeError');

error = undefined;

try {
    new PipeSocket();
} catch (e) {
    error = e;
}
isNot(error, undefined, 'PipeSocket() should throw');
eq(error.name, 'TypeError', 'PipeSocket() should throw TypeError');
