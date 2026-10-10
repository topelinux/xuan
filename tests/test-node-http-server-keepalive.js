import assert from 'tjs:assert';

function sleep(ms) {
    return new Promise((resolve) => setTimeout(resolve, ms));
}

const code = `
const http = require('node:http');
const server = http.createServer((req, res) => res.end('ok'));
server.listen(0, () => {});
`;

const proc = tjs.spawn([ tjs.exePath, '-e', code ], { stdout: 'ignore', stderr: 'pipe' });
const waitPromise = proc.wait();
const early = await Promise.race([
    waitPromise.then((status) => ({ type: 'exit', status })),
    sleep(400).then(() => ({ type: 'timeout' })),
]);

if (early.type === 'exit') {
    const errStr = await proc.stderr.text();
    throw new Error(`node:http server exited early: ${JSON.stringify(early.status)} stderr=${errStr}`);
}

proc.kill('SIGTERM');
const status = await waitPromise;
assert.ok(status.term_signal === 'SIGTERM' || status.exit_status === 0 || status.exit_status === 1);
