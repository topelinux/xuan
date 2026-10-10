import assert from 'tjs:assert';
import { spawnSync } from 'node:child_process';

const script = `
    const stream = new ReadableStream({
        start(controller) {
            controller.enqueue(new TextEncoder().encode('test'));
            controller.close();
        }
    });
    await fetch('https://postman-echo.com/post', {
        method: 'POST',
        body: stream
    });
`;

const ret = spawnSync(process.execPath, [ 'eval', script ], { encoding: 'utf8' });
assert.eq(ret.exit_status ?? ret.status, 1);
assert.ok(ret.stderr.includes('duplex'), 'error mentions duplex');
